#include "store.hpp"

#include "thirdparty/MurmurHash3.h"

using namespace std;
namespace bohrium {
namespace engine {
namespace cpu {

Store::Store(const string object_directory, const string kernel_directory) 
: object_directory(object_directory), kernel_directory(kernel_directory), kernel_prefix("KRN_"), library_prefix("LIB_")
{
    //
    // Create an identifier with low collision...
    char uid[7];    
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    srand(getpid());
    for (int i = 0; i < 7; ++i) {
        uid[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    uid[6] = 0;

    this->uid = string(uid);
}

Store::~Store()
{
    DEBUG("++ Store::~Store()");
    DEBUG("-- Store::~Store()");
}

string Store::text(void)
{
    stringstream ss;
    ss << "Store(\"" << object_directory << "\") : uid(" << this->uid << ");" << endl;
    return ss.str();
}

/**
 *  Get the id of the store.
 */
string Store::get_uid(void)
{
    DEBUG("   Store::get_uid(void) : uid(" << this->uid << ");");
    return this->uid;
}

string Store::obj_abspath(string symbol)
{
    return  this->object_directory  +\
            "/"                     +\
            this->kernel_prefix     +\
            symbol                  +\
            "_"                     +\
            this->uid               +\
            ".so";
}

string Store::src_abspath(string symbol)
{
    return  this->kernel_directory  +\
            "/"                     +\
            this->kernel_prefix     +\
            symbol                  +\
            "_"                     +\
            this->uid               +\
            ".c";
}

/**
 *  Check that the given symbol has an object ready.
 */
bool Store::symbol_ready(string symbol)
{
    DEBUG("   Store::symbol_ready("<< symbol << ") : return(" << (funcs.count(symbol) > 0) << ");");
    return funcs.count(symbol) > 0;
}

/**
 *  Construct a mapping of all symbols and from where they can be loaded.
 *  Populates compiler->libraries
 */
size_t Store::preload(void)
{
    DEBUG("Store::preload(void)");
    DIR *dir;
    struct dirent *ent;
    bool res = true;
    size_t nloaded = 0;

    //
    //  Index of a library containing multiple symbols
    //
    //  The file:
    //  LIB_[a-z0-9]+_xxxxxx.idx
    //
    //  Contains a new-line delimited list of names such as:
    //
    //  KRN_\d+_xxxxxx
    //
    //  Which can be loaded from:
    //
    //  LIB_[a-z0-9]+_xxxxxx.so
    //
    if ((dir = opendir(object_directory.c_str())) != NULL) {
        DEBUG("Store::preload(...) -- GOING MULTI!");
        while ((ent = readdir (dir)) != NULL) {     // Go over dir-entries
            size_t fn_len = strlen(ent->d_name);
            if (fn_len<14) {
                continue;
            }
            string filename(ent->d_name);
            DEBUG("We have a potential: " << filename);
            string symbol;                     // KRN_\d+
            string library;                    // LIB_whatever_xxxxxx.so

            if (0==filename.compare(fn_len-4, 4, ".idx")) {
                string basename;
                basename.assign(filename, 0, filename.length()-4);

                // Library
                library = object_directory  +\
                          "/"               +\
                          basename          +\
                          ".so";
               
                // Fill path to index filename 
                string index_fn = object_directory  +\
                                  "/"               +\
                                  filename;
                DEBUG("MULTI: " << library << " ||| " << index_fn);
                ifstream symbol_file(index_fn);
                for(string symbol; getline(symbol_file, symbol) && res;) {
                    if (0==libraries.count(symbol)) {
                        add_symbol(symbol, library);
                    }
                }
                symbol_file.close();
            }
        }
        closedir (dir);
    } else {
        throw runtime_error("Failed opening object-path.");
    }

    //
    //  A library containing a single symbol, the filename
    //  provides the symbol based on the naming convention:
    //
    //  KRN_[\d+]_XXXXXX.so
    //
    if ((dir = opendir (object_directory.c_str())) != NULL) {
        DEBUG("GOING SINGLE!");
        while((ent = readdir(dir)) != NULL) {
            size_t fn_len = strlen(ent->d_name);
            if (fn_len<14) {
                continue;
            }
            string filename(ent->d_name);
            string symbol;                     // BH_ADD_fff_CCC_3d
            string library;                    // BH_ADD_fff_CCC_3d_yAycwd
           
            // Must begin with "KRN_" 
            // Must end with ".so"
            if ((0==filename.compare(0, this->kernel_prefix.length(), this->kernel_prefix)) && \
                (0==filename.compare(fn_len-3, 3, ".so"))) {

                // Construct the abspath for the library
                library = object_directory  +\
                          "/"               +\
                          filename;

                // Extract the symbol "KRN_(d+)_xxxxxx.so"
                symbol.assign(
                    filename,
                    this->kernel_prefix.length(),
                    fn_len-10-this->kernel_prefix.length()
                );
                DEBUG("[SYMBOL="<< symbol <<",LIBRARY="<<library<<"]");

                if (0==libraries.count(symbol)) {
                    add_symbol(symbol, library);
                }
            }
        }
        closedir (dir);
    } else {
        throw runtime_error("Failed opening object-path.");
    }

    //
    // This is the part that actually loads them...
    // This could be postponed...
    map<string, string>::iterator it;    // Iterator
    for(it=libraries.begin(); (it != libraries.end()) && res; ++it) {
        res = load(it->first, it->second);
        nloaded += res;
    }

    DEBUG("Store::preload(void) : nloaded("<< nloaded << ");");

    return nloaded;
}

void Store::add_symbol(string symbol, string library)
{
    DEBUG("Store::add_symbol("<< symbol <<", "<< library <<");");
    libraries.insert(pair<string, string>(symbol, library));
}

/**
 *  Load a single symbol from library symbol into func-storage.
 */
bool Store::load(string symbol) {
    DEBUG("   Store::load("<< symbol << ");");

    return load(symbol, libraries[symbol]);
}

bool Store::load(string symbol, string library)
{
    DEBUG("   Store::load("<< symbol << ", " << library << ");");
    
    char *error_msg = NULL;             // Buffer for dlopen errors
    int errnum = 0;
    
    if (0==handles.count(library)) {    // Open library
        handles[library] = dlopen(
            library.c_str(),
            RTLD_NOW
        );
        errnum = errno;
    }
    if (!handles[library]) {            // Check that it opened
        utils::error(
            errnum,
            "Failed openening library; dlopen(filename='%s', RTLF_NOW) failed.",
            library.c_str()
        );
        return false;
    }

    dlerror();                          // Clear any existing error then,
    funcs[symbol] = (func)dlsym(        // Load symbol/function
        handles[library],
        (kernel_prefix+symbol).c_str()
    );
    error_msg = dlerror();
    if (error_msg) {
        utils::error(
            error_msg,
            "dlsym( handle='%s', symbol='%s' )\n",
            library.c_str(),
            symbol.c_str()
        );
        //free(error_msg); TODO: This should not be freed!?
        return false;
    }
    return true;
}

}}}
