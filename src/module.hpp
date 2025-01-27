#ifndef MODULE_HPP_INCLUDED
#define MODULE_HPP_INCLUDED

#include "filesystem.hpp"
#include "variant.hpp"

#include <string>
#include <vector>

namespace module {

struct modules {
	std::string name_;
	std::string pretty_name_;
	std::string abbreviation_;
	std::string base_path_;
};

const std::string get_module_name();
const std::string get_module_pretty_name();
std::string map_file(const std::string& fname);
void get_unique_filenames_under_dir(const std::string& dir,
                                    std::map<std::string, std::string>* file_map);

void get_files_in_dir(const std::string& dir,
                      std::vector<std::string>* files,
                      std::vector<std::string>* dirs=NULL,
                      sys::FILE_NAME_MODE mode=sys::FILE_NAME_ONLY);

std::string get_id(const std::string& id);
std::string get_module_id(const std::string& id);
std::string make_module_id(const std::string& name);
std::map<std::string, std::string>::const_iterator find(const std::map<std::string, std::string>& filemap, const std::string& name);
const std::string& get_module_path(const std::string& abbrev);
std::vector<variant> get_all();
variant get(const std::string& name);
void load(const std::string& name, bool initial=true);

}

#endif
