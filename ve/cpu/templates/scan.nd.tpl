// Scan operation of a strided n-dimensional array where n>1
// TODO: openmp
//       dimension-based optimizations
//       loop collapsing...
{
    const int64_t nelements = iterspace->nelem;
    {{ATYPE}} axis = *{{OPD_IN2}}_first;

    const int64_t last_e      = nelements-1;
    const int64_t shape_axis  = iterspace->shape[axis];
    const int64_t ndim        = iterspace->ndim;

    const int mthreads = omp_get_max_threads();
    const int64_t nworkers = nelements > mthreads ? mthreads : 1;

    int64_t coord[CPU_MAXDIM];
    memset(coord, 0, CPU_MAXDIM * sizeof(int64_t));

    //
    //  Walk over the output
    //
    int64_t cur_e = 0;
    while (cur_e <= last_e) {

        //
        // Compute offset based on coordinate
        //
        {{ETYPE}}* a{{OPD_OUT}}_current = a{{OPD_OUT}}_first;
        {{ETYPE}}*  a{{OPD_IN1}}_current = a{{OPD_IN1}}_first;

        for (int64_t j=0; j<ndim; ++j) {           
            a{{OPD_OUT}}_current += coord[j] * a{{OPD_OUT}}_stride[j];
            a{{OPD_IN1}}_current += coord[j] * a{{OPD_IN1}}_stride[j];
        }

        //
        // Iterate over axis dimension
        //
        {{ETYPE}} accu = ({{ETYPE}}){{NEUTRAL_ELEMENT}};
        for (int64_t j = 0; j<shape_axis; ++j) {
            {{PAR_OPERATIONS}}
            
            // Update the output
            *a{{OPD_OUT}}_current = accu;
            
            a{{OPD_OUT}}_current += a{{OPD_OUT}}_stride_axis;
            a{{OPD_IN1}}_current += a{{OPD_IN1}}_stride_axis;
        }
        cur_e += shape_axis;

        // Increment coordinates for the remaining dimensions
        for (int64_t j = ndim-1; j >= 0; --j) {
            if (j==axis) {      // Skip the axis dimension
                continue;       // It is calculated within the loop above
            }
            coord[j]++;         // Still within this dimension
            if (coord[j] < iterspace->shape[j]) {       
                break;
            } else {            // Reached the end of this dimension
                coord[j] = 0;   // Reset coordinate
            }                   // Loop then continues to increment the next dimension
        }
    }
    // TODO: Handle write-out of non-temp and non-const scalars.
}

