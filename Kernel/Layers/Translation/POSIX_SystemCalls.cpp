/**
 * File: POSIX_SystemCalls.cpp
 * Author: Marwan Mostafa
 * Description: Placeholder POSIX syscall-number switch for translation layer work.
 */

#include <stdint.h>
#include "TranslationLayer.hpp"

/**
 * Function: TranslationLayer::HandlePosixSystemCallNumber
 * Description: Dispatches a POSIX/Linux syscall number to a placeholder switch case.
 * Parameters:
 *   uint64_t SystemCallNumber - POSIX/Linux syscall number to dispatch.
 *   uint64_t Arg1 - First syscall argument.
 *   uint64_t Arg2 - Second syscall argument.
 *   uint64_t Arg3 - Third syscall argument.
 *   uint64_t Arg4 - Fourth syscall argument.
 *   uint64_t Arg5 - Fifth syscall argument.
 *   uint64_t Arg6 - Sixth syscall argument.
 * Returns:
 *   void - Does not return a value.
 */

int64_t TranslationLayer::HandlePosixSystemCallNumber(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6)
{
    (void) Arg3;
    (void) Arg4;
    (void) Arg5;
    (void) Arg6;

    constexpr int64_t LINUX_ERR_ENOSYS = -38;

    if (SystemCallNumber == 2)
    {
        return HandleOpenSystemCall(reinterpret_cast<const char*>(Arg1), Arg2);
    }

    return LINUX_ERR_ENOSYS;
}
