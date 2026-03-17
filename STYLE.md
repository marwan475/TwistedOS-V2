# TwistedOS Coding Style Rules

This document defines repository-wide style rules currently in use.

## 1) No Magic Numbers

- Do not place unexplained numeric literals directly in code logic.
- Use named constants (`constexpr`, `const`, `enum`, or `#define` where appropriate) with clear intent.
- If a numeric literal is required (for example, protocol or hardware-defined values), document it clearly near its definition.

## 2) Function Header Format

Every function should include this header format immediately above its definition:

```cpp
/**
 * Function: <function_name>
 * Description: <what the function does>
 * Parameters:
 *   <type> <param_name> - <description>
 *   <type> <param_name> - <description>
 * Returns:
 *   <return_type> - <description>
 */
```

## 3) File Header Format

Every source/header file should include this header near the top of the file:

```cpp
/**
 * File: <file_name>
 * Author: <your_name>
 * Description: <what this file contains / purpose of the file>
 */
```

## Notes

- Keep descriptions brief and precise.
- Update headers when behavior or responsibilities change.

## 4) Keep Comments Limited and Useful

- Avoid excessive inline comments.
- Prefer self-explanatory code (clear naming and structure) over comment-heavy code.
- Add comments only when explaining non-obvious intent, hardware constraints, or important edge cases.

## 5) Use Descriptive Variable Names

- Variable names must clearly communicate purpose.
- Avoid vague names like `tmp`, `val`, `data`, or `x` unless in very small/localized contexts where meaning is obvious.
- Favor readable names that reduce the need for extra comments.

## 6) Acronym Rules for Naming

- Do not use acronyms in class names.
- Acronyms are allowed only in class instance variable names when the class name itself is fully written and descriptive.
- Do not use acronym-based names for normal variables (local, parameter, or global).

Examples:

- Preferred class name: `PhysicalMemoryManager`
- Not allowed class name: `PhysMemMgr`
- Allowed class instance variable: `PMM` 

## 7) Formatting

- Use `make format` to format the codebase.
- `make format` uses `clang-format` and is the required formatter for this repository.




