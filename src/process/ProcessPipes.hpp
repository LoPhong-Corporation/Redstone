#pragma once

// Creating a child process with redirected stdin/stdout/stderr requires
// three separate anonymous pipes, each with exactly one end inheritable
// (the end that becomes STARTUPINFOW::hStdInput/hStdOutput/hStdError) and
// one end kept private to the parent. Getting the inheritance flags wrong
// is the single most common source of handle leaks and "works once, then
// mysteriously stops working" bugs in this kind of code, so it is isolated
// here behind one well-tested function rather than repeated inline.

#include "core/Types.hpp"

namespace process
{

struct ProcessPipes
{
    core::UniqueHandle childStdinRead;   // -> STARTUPINFOW::hStdInput
    core::UniqueHandle parentStdinWrite; // parent writes console commands here

    core::UniqueHandle childStdoutWrite; // -> STARTUPINFOW::hStdOutput
    core::UniqueHandle parentStdoutRead; // parent reads console output here

    core::UniqueHandle childStderrWrite; // -> STARTUPINFOW::hStdError
    core::UniqueHandle parentStderrRead; // parent reads console errors here
};

// Creates all three pipe pairs. Returns false (and logs via core::Logger)
// if any CreatePipe/SetHandleInformation call fails.
bool CreateProcessPipes(ProcessPipes& pipes);

} // namespace process
