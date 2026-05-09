#pragma once

#include "ast.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct FunctionSignature {
    std::vector<TypeInfo> parameter_types;
    TypeInfo return_type{TypeKind::Unit, ""};
    bool is_builtin = false;
};

struct StructSignature {
    std::unordered_map<std::string, TypeInfo> fields;
};

class TypeScopeStack {
  public:
    using Scope = std::unordered_map<std::string, TypeInfo>;

    void Push();
    void Pop();
    void Declare(const std::string& name, TypeInfo type, const SourceLocation& location);
    TypeInfo Get(const std::string& name, const SourceLocation& location) const;

  private:
    std::vector<Scope> scopes_;
};

class TypeScopeFrame {
  public:
    explicit TypeScopeFrame(TypeScopeStack& scopes);
    ~TypeScopeFrame();

  private:
    TypeScopeStack& scopes_;
};

class TypeChecker {
  public:
    explicit TypeChecker(const Program& program);
    void Check();

  private:
    const Program& program_;
    std::unordered_map<std::string, FunctionSignature> function_signatures_;
    std::unordered_map<std::string, StructSignature> struct_signatures_;
    std::string current_function_name_;
    std::optional<std::string> current_module_name_;
    TypeInfo current_return_type_{TypeKind::Unit, ""};

    void RegisterBuiltins();
    void CollectStructDeclarations();
    void CollectFunctionSignatures();
    void CheckMainSignature() const;
    void CheckFunction(const FunctionDecl& function);
    bool CheckStatements(const std::vector<std::unique_ptr<Stmt>>& statements, TypeScopeStack& scopes);
    bool CheckStatement(const Stmt* stmt, TypeScopeStack& scopes);
    bool CheckScopedStatement(const Stmt* stmt, TypeScopeStack& scopes);
    TypeInfo CheckExpression(const Expr* expr, TypeScopeStack& scopes);

    static std::string GetFullFunctionName(const FunctionDecl& function);
    static std::string GetFullStructName(const StructDecl& struct_decl);
    std::string ResolveFunctionName(const std::string& raw_name) const;
    std::string ResolveMethodName(const TypeInfo& receiver_type, const std::string& method_name, const SourceLocation& location) const;
    TypeInfo ResolveTypeName(const std::string& raw_name, const std::optional<std::string>& module_name, const SourceLocation& location) const;
};
