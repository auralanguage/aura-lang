# Module System

Aura uses a file-based module system with qualified symbol access. This document explains how modules work, how imports are resolved, and best practices for organizing code.

## Module Declaration

Every Aura file that exports symbols must declare a module name:

```aura
module my_module;

// Functions, structs, etc. are now part of my_module
fn greet() {
    print("Hello from my_module!");
}
```

### Rules
- Module declaration must be the first non-comment line
- Module name must be a valid identifier
- One module per file
- Files without module declarations cannot be imported

## Importing Modules

Import statements load other files and make their symbols available:

```aura
import "helper.aura";
import "utils/math.aura";
```

### Import Syntax
- `import "path";`
- Path is relative to the importing file's directory
- File extension `.aura` is required
- No wildcard imports

### Import Resolution
1. Path is resolved relative to the importing file
2. File is loaded and parsed
3. Module declaration is validated
4. Symbols are registered under the module name
5. Recursive imports are processed

## Qualified Symbol Access

Imported symbols are accessed using module qualification:

```aura
// helper.aura
module helper;

fn add(a: Int, b: Int) -> Int {
    return a + b;
}

struct Point {
    x: Int,
    y: Int
}

// main.aura
import "helper.aura";

fn main() {
    let sum = helper::add(5, 3);
    let p = helper::Point { x: 10, y: 20 };
    print(sum);
}
```

### Qualification Rules
- `module_name::symbol_name`
- Works for functions, structs, and methods
- Required for all imported symbols
- No unqualified access to imported symbols

## Module Search Path

### Relative Imports
Imports are always relative to the importing file:

```
project/
├── main.aura
├── utils/
│   ├── math.aura
│   └── string.aura
└── models/
    └── user.aura
```

```aura
// main.aura
import "utils/math.aura";
import "models/user.aura";

fn main() {
    let result = utils::math::add(1, 2);
    let user = models::user::User { name: "Alice" };
}
```

### Absolute Paths
You can use absolute paths if needed:

```aura
import "C:/projects/aura/utils/math.aura";
```

### Working Directory
- Relative paths resolve from the file containing the import
- Not from the current working directory
- Consistent across different execution contexts

## Circular Import Detection

Aura detects and prevents circular imports:

```aura
// a.aura
module a;
import "b.aura";  // This would create a cycle

// b.aura
module b;
import "a.aura";  // if this imports a.aura
```

### Detection
- Build-time analysis of import graph
- Fails fast with clear error message
- Prevents infinite loading loops

## Module Contents

### Exported Symbols
All top-level declarations are exported:
- Functions
- Struct types
- Struct methods

### Private Symbols
No private declarations - everything in a module is public.

### Name Conflicts
- Symbols are qualified by module name
- No global namespace conflicts
- Different modules can have same symbol names

## Struct Field Namespacing

Currently, struct fields are not namespaced:

```aura
// shapes.aura
module shapes;

struct Circle {
    radius: Int
}

struct Rectangle {
    width: Int,
    height: Int
}

// main.aura
import "shapes.aura";

fn main() {
    let c = shapes::Circle { radius: 5 };
    let r = shapes::Rectangle { width: 10, height: 20 };

    // Field access is unqualified
    print(c.radius);      // OK
    print(r.width);       // OK
}
```

### Current Limitation
- Field names are not qualified by module
- Potential for name conflicts in large projects
- Planned for future enhancement

## Standard Library

Aura has a minimal built-in standard library:

### Available Without Import
- `print()` - Output to console
- Collection operations: `len()`, `push()`, `pop()`, etc.
- String operations: `contains()`, `join()`, etc.
- File operations: `file_exists()`, `read_text()`

### No External Dependencies
- All standard library functions are built-in
- No need to import standard library modules
- Available in all contexts

## Project Organization

### Recommended Structure
```
my_project/
├── main.aura          # Entry point
├── lib/
│   ├── utils.aura     # Utility functions
│   ├── math.aura      # Math helpers
│   └── io.aura        # I/O helpers
├── models/
│   ├── user.aura      # User model
│   └── product.aura   # Product model
└── tests/
    ├── utils_test.aura
    └── models_test.aura
```

### Entry Point
- Main file should contain `fn main()`
- Can import other modules
- Executed when running `aura run main.aura`

### Test Organization
- Test files can import production modules
- Use descriptive names: `*_test.aura`
- Run with `aura test`

## Import Best Practices

### 1. Use Relative Paths
```aura
// Good
import "utils/math.aura";

// Avoid absolute paths unless necessary
import "C:/projects/utils/math.aura";
```

### 2. Group Related Imports
```aura
// Group imports at the top
import "utils/math.aura";
import "utils/string.aura";
import "models/user.aura";
```

### 3. Use Clear Module Names
```aura
// Good
module user_validation;
module payment_processor;

// Avoid generic names
module utils;  // Too vague
```

### 4. Avoid Deep Nesting
```aura
// Prefer
import "utils/math.aura";
utils::math::add(a, b);

// Over deep qualification
import "utils/math/arithmetic/basic.aura";
utils::math::arithmetic::basic::add(a, b);
```

## Error Handling

### Common Import Errors

#### File Not Found
```
Error: Cannot import "missing.aura": file not found
```

#### Module Declaration Missing
```
Error: Imported file "helper.aura" does not declare a module
```

#### Circular Import
```
Error: Circular import detected: a.aura -> b.aura -> a.aura
```

#### Symbol Not Found
```
Error: Undefined symbol 'helper::missing_function'
```

### Debugging Imports
- Use `aura check file.aura` to validate imports without running
- Check file paths are correct
- Ensure module declarations exist
- Verify symbol names and qualification

## Future Enhancements

### Planned Features
- Package management system
- Versioned dependencies
- Private module members
- Module aliases
- Glob imports
- Standard library modules

### Current Limitations
- No separate compilation
- All imports loaded at once
- No conditional imports
- No re-exports

## Examples

### Simple Module
```aura
// calculator.aura
module calculator;

fn add(a: Int, b: Int) -> Int {
    return a + b;
}

fn multiply(a: Int, b: Int) -> Int {
    return a + b;  // Oops, should be *
}
```

### Using the Module
```aura
// main.aura
import "calculator.aura";

fn main() {
    let sum = calculator::add(5, 3);
    let product = calculator::multiply(5, 3);
    print("Sum: " + sum);        // 8
    print("Product: " + product); // 8 (bug!)
}
```

### Struct Module
```aura
// geometry.aura
module geometry;

struct Point {
    x: Int,
    y: Int

    fn distance_from_origin(self: Point) -> Int {
        return self.x * self.x + self.y * self.y;  // Simplified
    }
}

fn create_point(x: Int, y: Int) -> Point {
    return Point { x: x, y: y };
}
```

### Using Struct Module
```aura
// draw.aura
import "geometry.aura";

fn main() {
    let p = geometry::create_point(3, 4);
    let dist = p.distance_from_origin();
    print("Distance: " + dist);
}
```
