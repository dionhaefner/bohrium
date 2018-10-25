/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the
GNU Lesser General Public License along with Bohrium.

If not, see <http://www.gnu.org/licenses/>.
*/

#include <jitk/engines/engine.hpp>

using namespace std;

namespace bohrium {
namespace jitk {

void Engine::writeKernelFunctionArguments(const jitk::SymbolTable &symbols,
                                          std::stringstream &ss,
                                          const char *array_type_prefix) {
    // We create the comma separated list of args and saves it in `stmp`
    std::stringstream stmp;
    for (size_t i = 0; i < symbols.getParams().size(); ++i) {
        bh_base *b = symbols.getParams()[i];
        if (array_type_prefix != nullptr) {
            stmp << array_type_prefix << " ";
        }
        stmp << writeType(b->type) << "* __restrict__ a" << symbols.baseID(b) << ", ";
    }

    for (const bh_view *view: symbols.offsetStrideViews()) {
        stmp << writeType(bh_type::UINT64);
        stmp << " vo" << symbols.offsetStridesID(*view) << ", ";
        for (int i = 0; i < view->ndim; ++i) {
            stmp << writeType(bh_type::UINT64) << " vs" << symbols.offsetStridesID(*view) << "_" << i << ", ";
        }
    }

    if (not symbols.constIDs().empty()) {
        for (auto it = symbols.constIDs().begin(); it != symbols.constIDs().end(); ++it) {
            const InstrPtr &instr = *it;
            stmp << "const " << writeType(instr->constant.type) << " c" << symbols.constID(*instr) << ", ";
        }
    }

    // And then we write `stmp` into `ss` excluding the last comma
    const std::string strtmp = stmp.str();
    if (strtmp.empty()) {
        ss << "()";
    } else {
        // Excluding the last comma
        ss << "(" << strtmp.substr(0, strtmp.size() - 2) << ")";
    }
}

void Engine::writeBlock(const SymbolTable &symbols,
                        const Scope *parent_scope,
                        const LoopB &kernel,
                        const std::vector<uint64_t> &thread_stack,
                        bool opencl,
                        std::stringstream &out) {

    if (kernel.isSystemOnly()) {
        out << "// Removed loop with only system instructions\n";
        return;
    }

    jitk::Scope scope(symbols, parent_scope);

    // Declare temporary arrays
    {
        const set<bh_base *> &local_tmps = kernel.getLocalTemps();
        for (const jitk::InstrPtr &instr: iterator::allInstr(kernel)) {
            for (const auto &view: instr->getViews()) {
                if (util::exist(local_tmps, view.base)) {
                    if (not (scope.isDeclared(view) or symbols.isAlwaysArray(view.base))) {
                        scope.insertTmp(view.base);
                        util::spaces(out, 8 + kernel.rank * 4);
                        scope.writeDeclaration(view, writeType(view.base->type), out);
                        out << "\n";
                    }
                }
            }
        }
    }

    // Let's declare indexes if we are not at the kernel level (rank == -1)
    if (kernel.rank >= 0) {
        for (const InstrPtr &instr: iterator::allLocalInstr(kernel)) {
            for (size_t i = 0; i < instr->operand.size(); ++i) {
                const bh_view &view = instr->operand[i];
                if (symbols.existIdxID(view) and scope.isArray(view)) {
                    if (not scope.isIdxDeclared(view)) {
                        util::spaces(out, 8 + kernel.rank * 4);
                        int hidden_axis = BH_MAXDIM;
                        if (i == 0 and bh_opcode_is_reduction(instr->opcode)) {
                            hidden_axis = instr->sweep_axis();
                        }
                        scope.writeIdxDeclaration(view, writeType(bh_type::UINT64), hidden_axis, out);
                        out << "\n";
                    }
                }
            }
        }
    }

    // Declare scalar replacement of outputs that reduces over the innermost axis in the child block
    {
        for (const jitk::Block &b1: kernel._block_list) {
            if (not b1.isInstr()) {
                for (const InstrPtr &instr: iterator::allLocalInstr(b1.getLoop())) {
                    if (bh_opcode_is_reduction(instr->opcode) and jitk::sweeping_innermost_axis(instr)) {
                        const bh_view &view = instr->operand[0];
                        if (not(scope.isDeclared(view) or symbols.isAlwaysArray(view.base))) {
                            scope.insertScalarReplaced(view);
                            util::spaces(out, 8 + kernel.rank * 4);
                            scope.writeDeclaration(view, writeType(view.base->type), out);
                            out << "// For reductions inner-most";
                            out << "\n";
                        }
                    }
                }
            }
        }
    }

    // Declare scalar-replacement because of duplicates
    {
        std::set<bh_base *> ignore_bases;
        for (const InstrPtr &instr: iterator::allLocalInstr(kernel)) {
            if (bh_opcode_is_accumulate(instr->opcode)) {
                ignore_bases.insert(instr->operand[1].base);
            }
            if (bh_opcode_is_reduction(instr->opcode)) {
                ignore_bases.insert(instr->operand[0].base);
                ignore_bases.insert(instr->operand[1].base);
            }
            for (const bh_view &view: instr->getViews()) {
                if (symbols.isAlwaysArray(view.base)) {
                    ignore_bases.insert(view.base);
                }
            }
        }

        // First we add a valid view to the set of 'candidates' and if we encounter the view declare it
        std::set<bh_view> candidates;
        for (const InstrPtr &instr: iterator::allLocalInstr(kernel)) {
            for (size_t i = 0; i < instr->operand.size(); ++i) {
                const bh_view &view = instr->operand[i];
                if (not (view.isConstant() or util::exist(ignore_bases, view.base) or scope.isDeclared(view))) {
                    if (util::exist(candidates, view)) {
                        scope.insertScalarReplaced(view);
                        util::spaces(out, 8 + kernel.rank * 4);
                        scope.writeDeclaration(view, writeType(view.base->type), out);
                        // Let's load data into the scalar-replaced variable
                        if (not (i == 0 and instr->constructor)) { // No need to load data into a new output
                            out << " " << scope.getName(view) << " = a" << symbols.baseID(view.base);
                            if (i == 0 and bh_opcode_is_reduction(instr->opcode)) {
                                write_array_subscription(scope, view, out, false, instr->sweep_axis());
                            } else {
                                write_array_subscription(scope, view, out, false);
                            }
                            out << ";";
                        }
                        out << "// For duplicate access\n";
                    } else {
                        candidates.insert(view);
                    }
                }
            }
        }
    }

    // Write the for-loop body
    // The body in OpenCL and OpenMP are very similar but OpenMP might need to insert "#pragma omp atomic/critical"
    if (opencl) {
        for (const Block &b: kernel._block_list) {
            if (b.isInstr()) { // Finally, let's write the instruction
                if (b.getInstr() != nullptr and not bh_opcode_is_system(b.getInstr()->opcode)) {
                    util::spaces(out, 4 + b.rank() * 4);
                    write_instr(scope, *b.getInstr(), out, true);
                }
            } else {
                util::spaces(out, 4 + b.rank() * 4);
                loopHeadWriter(symbols, scope, b.getLoop(), thread_stack, out);
                writeBlock(symbols, &scope, b.getLoop(), thread_stack, opencl, out);
                util::spaces(out, 4 + b.rank() * 4);
                out << "}\n";
            }
        }
    } else {
        for (const Block &b: kernel._block_list) {
            if (b.isInstr()) { // Finally, let's write the instruction
                const InstrPtr &instr = b.getInstr();
                if (not bh_opcode_is_system(instr->opcode)) {
                    if (instr->operand.size() > 0) {
                        if (scope.isOpenmpAtomic(instr)) {
                            util::spaces(out, 4 + b.rank() * 4);
                            out << "#pragma omp atomic\n";
                        } else if (scope.isOpenmpCritical(instr)) {
                            util::spaces(out, 4 + b.rank() * 4);
                            out << "#pragma omp critical\n";
                        }
                    }
                    util::spaces(out, 4 + b.rank() * 4);
                    write_instr(scope, *instr, out);
                }
            } else {
                util::spaces(out, 4 + b.rank() * 4);
                loopHeadWriter(symbols, scope, b.getLoop(), thread_stack, out);
                writeBlock(symbols, &scope, b.getLoop(), thread_stack, opencl, out);
                util::spaces(out, 4 + b.rank() * 4);
                out << "}\n";
            }
        }
    }

    // Let's copy the scalar replaced back to the original array
    {
        for (const InstrPtr &instr: iterator::allLocalInstr(kernel)) {
            if (not instr->operand.empty()) {
                const bh_view &view = instr->operand[0];
                if (scope.isScalarReplaced(view)) {
                    util::spaces(out, 8 + kernel.rank * 4);
                    out << "a" << symbols.baseID(view.base);
                    if (bh_opcode_is_reduction(instr->opcode)) {
                        write_array_subscription(scope, view, out, false, instr->sweep_axis());
                    } else {
                        write_array_subscription(scope, view, out, false);
                    }
                    out << " = ";
                    scope.getName(view, out);
                    out << ";\n";
                    scope.eraseScalarReplaced(view);
                }
            }
        }
    }
}

void Engine::setConstructorFlag(std::vector<bh_instruction *> &instr_list, std::set<bh_base *> &constructed_arrays) {
    for (bh_instruction *instr: instr_list) {
        instr->constructor = false;
        for (size_t o = 0; o < instr->operand.size(); ++o) {
            const bh_view &v = instr->operand[o];
            if (not v.isConstant()) {
                if (o == 0 and not util::exist_nconst(constructed_arrays, v.base)) {
                    instr->constructor = true;
                }
                constructed_arrays.insert(v.base);
            }
        }
    }
}

}
} // namespace
