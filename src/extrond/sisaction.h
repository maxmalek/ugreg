#pragma once

#include "variant.h"
#include <string>
#include <vector>
#include <regex>

class SISClient;

// One communication fragment
struct SISComm
{
public:
    SISComm();
    ~SISComm();

    bool parse(VarCRef a); // array

    // run on input buffer. returns how many bytes were consumed, >= 0 is valid.
    // < 0 is an error
    int exec(SISClient& client) const;

    size_t tableIndex;
    std::string paramStr;
    u64 paramNum;
    std::regex re;
};

// List of instructions for communicating a single operation
class SISAction
{
public:
    SISAction();
    bool parse(VarCRef a); // array

    int exec(SISClient& client) const; // FIXME: this should take a VM?

    std::vector<SISComm> comms;
};
