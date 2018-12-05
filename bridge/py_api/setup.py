#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
/*
This file is part of Bohrium and copyright (c) 2018 the Bohrium
<http://www.bh107.org>

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
"""

from setuptools import setup, Extension
from setuptools.command.build_py import build_py as setup_build_py
from distutils.dir_util import mkpath, copy_tree
from codecs import open
import platform
import os
import json
import pprint
import shutil
import glob
import subprocess
import re

""" Beside the regular setup arguments, this script reads the follow environment variables:

      * PY_API_SRC_ROOT - Path to the root of the cmake source directory
      * PY_API_BUILD_ROOT - Path to the root of the cmake build directory
      * PY_API_INSTALL_PREFIX - The `CMAKE_INSTALL_PREFIX` path
      * PY_API_WHEEL - Set this to OFF, if the config file and libraries shouldn't be included in the package.
"""

assert 'PY_API_INSTALL_PREFIX' in os.environ
_install_prefix = os.environ['PY_API_INSTALL_PREFIX']


def wheel_build():
    """Return true when we a doing a Python wheel build"""
    assert 'PY_API_WHEEL' in os.environ
    env = os.environ['PY_API_WHEEL']
    return env.lower() != "off"  # Anything other than "off" means "on"


def _copy_files(glob_str, dst_dir):
    """Copy files using the `glob_str` and copy to `dst_dir`"""
    mkpath(dst_dir)
    for fname in glob.glob(glob_str):
        if os.path.isfile(fname):
            if ".dylib" in fname:
                # We need this HACK because osx might not preserve the write and exec permission
                out_path = os.path.join(dst_dir, os.path.basename(fname))
                shutil.copyfile(fname, out_path)
                subprocess.check_call("chmod a+x %s" % out_path, shell=True)
            else:
                shutil.copy(fname, dst_dir)
            print("copy: %s => %s" % (fname, dst_dir))


def _copy_dirs(src_dir, dst_dir):
    """Copy dir"""
    mkpath(dst_dir)
    copy_tree(src_dir, dst_dir)
    print("copy %s => %s" % (src_dir, dst_dir))


def _regex_replace(pattern, repl, src):
    """Replacing matches in `src` with `repl` using regex `pattern`"""
    print ("config.ini: replacing: '%s' => '%s'" % (pattern, repl))
    return re.sub(pattern, repl, src)


def src_root(*paths):
    assert 'PY_API_SRC_ROOT' in os.environ
    return os.path.join(os.environ['PY_API_SRC_ROOT'], *paths)


def build_root(*paths):
    assert 'PY_API_BUILD_ROOT' in os.environ
    return os.path.join(os.environ['PY_API_BUILD_ROOT'], *paths)


def script_path(*paths):
    prefix = os.path.abspath(os.path.dirname(__file__))
    assert len(prefix) > 0
    return os.path.join(prefix, *paths)


# Returns the numpy data type name
def dtype_bh2np(bh_type_str):
    return bh_type_str[3:].lower()  # Remove BH_ and convert to lower case


def write_header(header_file):
    """Writes the bohrium_api header"""
    # Find all function in `_bh_api.c` that starts with `BhAPI_` and extract the signature
    func_list = []
    with open(src_root("bridge", "py_api", "src", "_bh_api.c")) as f:
        py_api = f.read().replace("\n", "").replace(";", "\n")
        match_list = re.findall(r'static(.+)BhAPI_(.+?)(\(.*\))\s*{', py_api)
        for m in match_list:
            func_list.append([i.strip() for i in m])

    func_macro = ""
    func_proto = ""
    func_assign = ""
    func_define = ""
    for i, func in enumerate(func_list):
        func_macro += "#define BhAPI_{0}_NUM {1}\n".format(func[1], i)
        func_macro += "#define BhAPI_{0}_RETURN {1}\n".format(func[1], func[0])
        func_macro += "#define BhAPI_{0}_PROTO {1}\n".format(func[1], func[2])
        func_proto += "static BhAPI_{0}_RETURN BhAPI_{0} BhAPI_{0}_PROTO;\n".format(func[1])
        func_assign += "    c_api_struct[BhAPI_{0}_NUM] = (void *)BhAPI_{0};\n".format(func[1])
        func_define += "#define BhAPI_{0} (*(BhAPI_{0}_RETURN (*)BhAPI_{0}_PROTO) PyBhAPI[BhAPI_{0}_NUM])\n".format(
            func[1])

    with open(build_root("bridge", "c", "out", "bhc_types.h")) as f:
        bhc_types = f.read()

    head = """\
#ifndef BH_API_H
#define BH_API_H

/* This file is auto generated by `setup.py`. See the `write_header()` function */

{BHC_TYPES}

#ifdef __cplusplus
extern "C" {
#endif

/* C API functions */
{MACRO}

/* Total number of C API pointers */
#define BhAPI_num_of_pointers {NUM}

#ifdef BhAPI_MODULE
/* This section is used when compiling _bh_api.c */

{PROTO}

static void init_c_api_struct(void *c_api_struct[]) {
{ASSIGN}
}

#else
/* This section is used in modules that use _bh_api.c's API */

{DEFINE}

#ifdef NO_IMPORT_BH_API
    extern void **PyBhAPI;
#else
    void **PyBhAPI;
    /* Return -1 on error, 0 on success.
     * PyCapsule_Import will set an exception if there's an error.
     */
    static int
    import_bh_api(void)
    {
        PyBhAPI = (void **)PyCapsule_Import("bohrium_api._C_API", 0);
        return (PyBhAPI != NULL) ? 0 : -1;
    }
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif /* !defined(BH_API_H) */
""".replace("{MACRO}", func_macro).replace("{NUM}", str(len(func_list))) \
        .replace("{PROTO}", func_proto).replace("{ASSIGN}", func_assign) \
        .replace("{DEFINE}", func_define).replace("{BHC_TYPES}", bhc_types)
    header_file.write(head)


# Let's find the version
cmd = "git describe --tags --long --match v[0-9]*"
try:
    # Let's get the Bohrium version without the 'v' and hash (e.g. v0.8.9-47-g6464 => v0.8.9-47)
    version = subprocess.check_output(cmd, shell=True, cwd=src_root()).decode('utf-8')
    version = re.match(".*v(.+-\d+)-", str(version)).group(1)
except subprocess.CalledProcessError as e:
    print("Couldn't find the Bohrium version through `git describe`, are we not in the git repos?\n"
          "Using the VERSION file instead")
    # The version if written in the VERSION file in the root of Bohrium
    with open(src_root("VERSION"), "r") as f:
        version = f.read().strip()
print("Bohrium version: %s" % version)

# Information variables that should be written to the _info.py file
info_vars = {'__version__': version}


def write_info(o):
    """Write the _info.py file to `o`"""
    o.write("# This file is auto generated by the setup.py\n")

    # Write the information variables
    o.write("\n# Info variables:\n")
    for (key, val) in info_vars.items():
        o.write("%s = '%s'\n" % (key, val))
    o.write("\n")

    ufunc = {}
    with open(src_root('core', 'codegen', 'opcodes.json'), 'r') as f:
        opcodes = json.loads(f.read())
        for op in opcodes:
            if not op['system_opcode']:
                # Convert the type signature to bhc names
                type_sig = []
                for sig in op['types']:
                    type_sig.append([dtype_bh2np(s) for s in sig])

                name = op['opcode'].lower()[3:]  # Removing BH_ and we have the NumPy and bohrium name
                ufunc[name] = {
                    'name': name,
                    'id': int(op['id']),
                    'nop': int(op['nop']),
                    'elementwise': bool(op['elementwise']),
                    'type_sig': type_sig
                }
    o.write("op = ")
    pp = pprint.PrettyPrinter(indent=2, stream=o)
    pp.pprint(ufunc)

    # Find and write all supported data types
    s = "["
    with open(src_root('core', 'codegen', 'types.json'), 'r') as f:
        types = json.loads(f.read())
        for t in types:
            if t['numpy'] == "unknown":
                continue
            s += "numpy.dtype('%s'), " % t['numpy']
        s = s[:-2] + "]\n"
    o.write("""def numpy_types():
    import numpy
    return %s
    """ % s)


def write_config_ini(o):
    """Write a modified config file to `o`"""

    # Write a modified config file to the Python package dir
    with open(build_root("config.ini"), "r") as f:
        config_str = f.read()

        # Unset the `cache_dir` option
        config_str = _regex_replace("cache_dir = .*", "cache_dir = ~/.bohrium/cache", config_str)

        # Set the JIT compiler to gcc
        config_str = _regex_replace("compiler_cmd = \".* -x c", "compiler_cmd = \"gcc -x c", config_str)

        # clang doesn't support some unneeded flags
        config_str = _regex_replace("-Wno-expansion-to-defined", "", config_str)
        config_str = _regex_replace("-Wno-pass-failed", "", config_str)

        # Replace `lib` and `lib64` with `.libs`
        config_str = _regex_replace("%s/lib/" % _install_prefix,
                                    "%s/.libs/" % _install_prefix, config_str)
        config_str = _regex_replace("%s/lib " % _install_prefix,
                                    "%s/.libs " % _install_prefix, config_str)
        config_str = _regex_replace("%s/lib64/" % _install_prefix,
                                    "%s/.libs/" % _install_prefix, config_str)
        config_str = _regex_replace("%s/lib64 " % _install_prefix,
                                    "%s/.libs " % _install_prefix, config_str)

        # Compile command: replace absolute include path with a path relative to {CONF_PATH}.
        config_str = _regex_replace("%s/share/bohrium/" % _install_prefix, "{CONF_PATH}/", config_str)

        # Replace absolute library paths with a relative path.
        config_str = _regex_replace("%s/" % _install_prefix, "./", config_str)
        o.write(config_str)


# We extend the build_py command to also generate the _info.py file and copy shared libraries into the package
class build_py(setup_build_py):
    def run(self):
        if not self.dry_run:
            target_dir = os.path.join(self.build_lib, 'bohrium_api')
            self.mkpath(target_dir)
            p = os.path.join(target_dir, '_info.py')
            print("Generating '%s'" % p)
            with open(p, 'w') as fobj:
                write_info(fobj)

            self.mkpath(os.path.join(target_dir, 'include'))
            p = os.path.join(target_dir, 'include', 'bohrium_api.h')
            print("Generating '%s'" % p)
            with open(p, 'w') as fobj:
                write_header(fobj)
            # We also need a copy in the build root so we know its path
            _copy_files(p, build_root("bridge", "py_api", "include"))

            # The build of npbackend depend on Random123 headers so we include them as well
            _copy_dirs(src_root("thirdparty", "Random123-1.09", "include"), os.path.join(target_dir, 'include'))

            # If we are doing a wheel build, we have to include a modified config.ini, shared libraries,
            # and kernel dependencies
            if wheel_build():
                p = os.path.join(target_dir, 'config.ini')
                print("Generating '%s'" % p)
                with open(p, 'w') as fobj:
                    write_config_ini(fobj)

                _copy_dirs(src_root("include", "jitk", "kernel_dependencies"),
                           os.path.join(target_dir, 'include', "kernel_dependencies"))
                _copy_files(build_root("*", "*", "libbh_*"), os.path.join(target_dir, ".libs"))

        setup_build_py.run(self)


# On MacOSX we add a rpath to the installation path so that `delocate` can locate `libbh.dylib`
clinks = []
setup_requires = []
if platform.system() == 'Darwin':
    clinks += ['-Xlinker', '-rpath', '-Xlinker', '%s/lib' % _install_prefix]
    setup_requires += ['gcc7']

setup(
    cmdclass={'build_py': build_py},
    name='bohrium_api',
    version=version,
    description='Bohrium Python API',
    long_description='Python API for the Bohrium project <www.bh107.org>',
    url='http://bh107.org',
    author='The Bohrium Team',
    author_email='contact@bh107.org',
    maintainer='Mads R. B. Kristensen',
    maintainer_email='madsbk@gmail.com',
    platforms=['Linux', 'OSX'],

    # Choose your license
    license='GPL',

    # See https://pypi.python.org/pypi?%3Aaction=list_classifiers
    classifiers=[
        # How mature is this project? Common values are
        #   3 - Alpha
        #   4 - Beta
        #   5 - Production/Stable
        'Development Status :: 4 - Beta',

        # Indicate who your project is intended for
        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',

        'Topic :: Scientific/Engineering',
        'Topic :: Software Development',

        # Pick your license as you wish (should match "license" above)
        'License :: OSI Approved :: GNU General Public License (GPL)',

        # Specify the Python versions you support here. In particular, ensure
        # that you indicate whether you support Python 2, Python 3 or both.
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: C',
    ],

    # What does your project relate to?
    keywords='Bohrium, bh107, Python, C, HPC, MPI, PGAS, CUDA, OpenCL, OpenMP',

    # Dependencies
    install_requires=setup_requires,
    setup_requires=setup_requires,

    # You can just specify the packages manually here if your project is
    # simple. Or you can use find_packages().
    packages=['bohrium_api'],

    ext_modules=[
        Extension(
            name='bohrium_api._bh_api',
            sources=[script_path('src', '_bh_api.c')],
            include_dirs=[
                build_root("bridge", "c", "out"),
                build_root("bridge", "py_api", "include"),
            ],
            libraries=['dl', 'bhc', 'bh', 'bhxx'],
            library_dirs=[
                build_root('bridge', 'c'),
                build_root('bridge', 'cxx'),
                build_root('core')
            ],
            extra_compile_args=["-std=c99"],
            extra_link_args=clinks,
        ),
    ],
)
