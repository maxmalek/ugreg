#pragma once

#include <string>
#include <vector>

// returns array of "NAME=value", in UTF-8 encoding
std::vector<std::string> enumerateEnvVars();
