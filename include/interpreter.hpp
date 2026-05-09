#pragma once

#include "ast.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct StructRuntimeInfo {
    std::unordered_map<std::string, bool> fields;
};

struct ReturnSignal {
    explicit ReturnSignal(Value value);
    Value value;
};

class ScopeStack {
  public:
    using Scope = std::unordered_map<std::string, Value>;

    void Push();
    void Pop();
    void Declare(const std::string& name, Value value);
    Value Get(const std::string& name) const;
    void Assign(const std::string& name, Value value);

  private:
    std::vector<Scope> scopes_;
};

class ScopeFrame {
  public:
    explicit ScopeFrame(ScopeStack& scopes);
    ~ScopeFrame();

  private:
    ScopeStack& scopes_;
};

class Interpreter {
  public:
    explicit Interpreter(const Program& program);
    bool HasFunction(const std::string& name) const;
    Value ExecuteMain();

  private:
    const Program& program_;
    std::unordered_map<std::string, const FunctionDecl*> functions_;
    std::unordered_map<std::string, StructRuntimeInfo> structs_;

    Value ExecuteFunction(const std::string& name, const std::vector<Value>& arguments);
    void ExecuteStatement(const Stmt* stmt, ScopeStack& scopes, const std::optional<std::string>& current_module_name);
    void ExecuteScopedStatement(const Stmt* stmt,
                                ScopeStack& scopes,
                                const std::optional<std::string>& current_module_name);
    Value Evaluate(const Expr* expr, ScopeStack& scopes, const std::optional<std::string>& current_module_name);

    static bool ExpectBool(const Value& value, const std::string& context);
    static long long ExpectInteger(const Value& value, const std::string& context);
    static Value EvaluateBinary(const Token& op, const Value& left, const Value& right);
    static std::string GetFullFunctionName(const FunctionDecl& function);
    static std::string GetFullStructName(const StructDecl& struct_decl);
    std::string ResolveFunctionName(const std::string& raw_name, const std::optional<std::string>& current_module_name) const;
    std::string ResolveMethodName(const std::string& struct_name, const std::string& method_name) const;
    std::string ResolveStructName(const std::string& raw_name, const std::optional<std::string>& current_module_name) const;
};
