# Language Specification

This document provides a formal specification of the Aura programming language, including syntax, type system, and builtin functions.

## Overview

Aura is a statically typed, imperative programming language with:
- C-style syntax with modern conveniences
- Static type checking
- Module system with qualified imports
- Built-in collection types and string handling
- Windows-first toolchain with standalone executable generation

## Lexical Structure

### Keywords
```
fn if else while for in return let struct module import
```

### Literals
- **Integers**: `42`, `-10`
- **Booleans**: `true`, `false`
- **Characters**: `'a'`, `'\n'`
- **Strings**: `"hello world"`, `"line 1\nline 2"`
- **Unit**: `()` (used for functions with no return value)

### Operators
- Arithmetic: `+`, `-`, `*`, `/`
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Logical: `&&`, `||`, `!`
- Assignment: `=`
- Member access: `.`
- Index access: `[]`
- Range: `..`
- Slice: `:`

### Identifiers
- Start with letter or underscore
- Contain letters, digits, underscores
- Case sensitive
- Cannot be keywords

## Syntax Grammar

### Program Structure
```
program ::= module-decl? import* item*

item ::= function | struct-decl

module-decl ::= "module" identifier ";"

import ::= "import" string-literal ";"
```

### Functions
```
function ::= "fn" identifier "(" param-list ")" ("->" type)? block

param-list ::= (parameter ("," parameter)*)?

parameter ::= identifier ":" type

block ::= "{" statement* "}"
```

### Statements
```
statement ::= let-stmt | assign-stmt | return-stmt | expr-stmt | if-stmt | while-stmt | for-stmt

let-stmt ::= "let" identifier (":" type)? "=" expression ";"

assign-stmt ::= expression "=" expression ";"

return-stmt ::= "return" expression? ";"

expr-stmt ::= expression ";"

if-stmt ::= "if" expression block ("else" block)?

while-stmt ::= "while" expression block

for-stmt ::= "for" pattern "in" expression block

pattern ::= identifier | (identifier "," identifier)
```

### Expressions
```
expression ::= equality

equality ::= comparison (("==" | "!=") comparison)*

comparison ::= term (("<" | "<=" | ">" | ">=") term)*

term ::= factor (("+" | "-") factor)*

factor ::= unary (("*" | "/") unary)*

unary ::= ("!" | "-") unary | call

call ::= primary ("(" arg-list? ")" | "[" expression "]" | "." identifier | ":" identifier)*

arg-list ::= expression ("," expression)*

primary ::= integer | boolean | char | string | identifier | "(" expression ")" | array-literal | struct-literal
```

### Types
```
type ::= "Int" | "Bool" | "Char" | "String" | "Unit" | "[" type "]" | identifier
```

### Literals and Constructors
```
array-literal ::= "[" expression ("," expression)* "]"

struct-literal ::= identifier "{" field-init ("," field-init)* "}"

field-init ::= identifier ":" expression
```

### Struct Declarations
```
struct-decl ::= "struct" identifier "{" field-decl* method-decl* "}"

field-decl ::= identifier ":" type ";"

method-decl ::= "fn" identifier "(" param-list ")" ("->" type)? block
```

## Type System

### Primitive Types
- **`Int`**: 64-bit signed integer
- **`Bool`**: Boolean values (`true`, `false`)
- **`Char`**: Unicode character
- **`String`**: UTF-8 string
- **`Unit`**: Unit type for side-effecting functions

### Collection Types
- **`[T]`**: Array/slice of type `T`
- Arrays are mutable, support indexing and slicing
- Slices are shared immutable views of arrays

### Struct Types
- User-defined types with named fields
- Support methods with `self` receiver
- Fields are mutable

### Type Rules
- Static type checking with inference for local variables
- No implicit conversions except:
  - `Char` + `String` → `String`
  - `String` + `Char` → `String`
  - `String == Char` and `Char == String` for equality
- Arrays and slices are covariant in their element type
- Functions have explicit parameter and return types

## Module System

### Module Declaration
```
module my_module;
```

### Imports
```
import "path/to/file.aura";
```

### Qualified Access
```
module_name::function_name()
module_name::TypeName
```

### Resolution Rules
- Files are loaded recursively from import statements
- Module names must be declared in imported files
- Symbols are qualified by their module name
- Circular imports are detected and rejected
- Relative paths resolve from the importing file's directory

## Control Flow

### Conditionals
```aura
if condition {
    // true branch
} else {
    // false branch
}
```

### Loops
```aura
while condition {
    // loop body
}

for item in collection {
    // iterate over elements
}

for index, item in collection {
    // iterate with indices
}
```

### Ranges
```aura
for i in 0..10 {
    // i goes from 0 to 9
}
```

## Collections

### Array Literals
```aura
let arr: [Int] = [1, 2, 3, 4, 5];
```

### Indexing
```aura
let first = arr[0];
arr[0] = 42;
```

### Slicing
```aura
let slice = arr[1:4];        // elements 1, 2, 3
let from_start = arr[:3];    // first 3 elements
let to_end = arr[2:];        // from index 2 to end
```

### Built-in Operations
- `len(collection)` - Get length
- `push(collection, element)` - Append element
- `pop(collection)` - Remove and return last element
- `insert(collection, index, element)` - Insert at index
- `remove_at(collection, index)` - Remove at index
- `clear(collection)` - Remove all elements

## Strings and Characters

### String Literals
```aura
let greeting = "Hello, World!";
```

### Character Literals
```aura
let ch = 'A';
```

### Operations
- `string[index]` → `Char`
- `string[start:end]` → `String` (shared view)
- `string1 + string2` → `String`
- `string + char` → `String`
- `char + string` → `String`

### Iteration
```aura
for ch in "hello" {
    // ch is Char
}

for index, ch in "hello" {
    // index is Int, ch is Char
}
```

## Builtin Functions

### Collection Operations
- `len(value: T) -> Int` - Length of arrays, strings, slices
- `[]` - Empty array literal, valid only when an explicit `[T]` context exists
- `push(collection: [T], element: T) -> Unit` - Append to array
- `pop(collection: [T]) -> T` - Remove and return last element
- `insert(collection: [T], index: Int, element: T) -> Unit` - Insert at index
- `remove_at(collection: [T], index: Int) -> T` - Remove and return element at index
- `clear(collection: [T]) -> Unit` - Remove all elements

### String Operations
- `contains(text: String, pattern: String) -> Bool` - Check if text contains pattern
- `contains(text: String, pattern: Char) -> Bool` - Check if text contains character
- `starts_with(text: String, prefix: String) -> Bool` - Check if text starts with prefix
- `starts_with(text: String, prefix: Char) -> Bool` - Check if text starts with character
- `ends_with(text: String, suffix: String) -> Bool` - Check if text ends with suffix
- `ends_with(text: String, suffix: Char) -> Bool` - Check if text ends with character
- `join(elements: [String], separator: String) -> String` - Join strings with separator
- `join(elements: [Char], separator: String) -> String` - Join characters with separator

### File Operations
- `file_exists(path: String) -> Bool` - Check if file exists
- `read_text(path: String) -> String` - Read entire file as string
- `write_text(path: String, text: String) -> Unit` - Overwrite a file with text
- `append_text(path: String, text: String) -> Unit` - Append text to a file
- `remove_file(path: String) -> Bool` - Remove a file, returns `true` if something was removed
- `create_dir(path: String) -> Bool` - Create a directory tree, returns `true` if a new directory was created
- `list_dir(path: String) -> [String]` - Return sorted entry names from a directory

### I/O Operations
- `print(value: T) -> Unit` - Print value to stdout

### Math Operations
- `abs(value: Int) -> Int` - Absolute value
- `min(a: Int, b: Int) -> Int` - Minimum of two integers
- `max(a: Int, b: Int) -> Int` - Maximum of two integers
- `pow(base: Int, exp: Int) -> Int` - Integer exponentiation (base raised to power exp)
  - Raises error if exponent is negative

## Examples

### Hello World
```aura
fn main() {
    print("Hello, World!");
}
```

### Struct with Methods
```aura
struct User {
    name: String,
    score: Int

    fn greet(self: User) -> Unit {
        print("Hello, " + self.name + "!");
    }

    fn boost(self: User, amount: Int) -> Int {
        self.score = self.score + amount;
        return self.score;
    }
}
```

### Module Usage
```aura
// In helper.aura
module helper;

fn add(a: Int, b: Int) -> Int {
    return a + b;
}

// In main.aura
import "helper.aura";

fn main() {
    let result = helper::add(5, 3);
    print(result);  // Prints: 8
}
```

### Collections and Loops
```aura
fn main() {
    let numbers: [Int] = [1, 2, 3, 4, 5];
    let empty: [Int] = [];
    let sum: Int = 0;

    for num in numbers {
        print(num);
    }

    for i in 0..len(numbers) {
        if numbers[i] == 2 || numbers[i] == 4 {
            sum = sum + numbers[i];
        }
    }

    print(sum);
}
```
