#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace c486cc {

class CompileError : public std::runtime_error {
public:
    explicit CompileError(const std::string& message) : std::runtime_error(message) {}
};

enum class TokenKind {
    End, Identifier, Integer, FloatLiteral, StringLiteral, CharLiteral,
    KwInt, KwChar, KwFloat, KwDouble, KwBool, KwVoid, KwStruct,
    KwIf, KwElse, KwWhile, KwFor, KwSwitch, KwCase, KwDefault, KwReturn,
    Plus, Minus, Star, Slash, Percent, Amp, Pipe, Caret, Bang, Tilde,
    Assign, Eq, Ne, Lt, Le, Gt, Ge, AndAnd, OrOr,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semicolon, Colon, Dot, Arrow
};

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourceLocation location;
};

class Lexer {
public:
    explicit Lexer(std::string source);
    Token next();
    std::vector<Token> all();
private:
    std::string source_;
    std::size_t pos_ = 0;
    SourceLocation loc_{};
    char peek(std::size_t offset = 0) const;
    char get();
    void skip_space_and_comments();
};

enum class TypeKind { Void, Bool, Char, Int, Float, Double, Pointer, Array, Struct };

struct Type {
    TypeKind kind = TypeKind::Void;
    std::string name;
    std::shared_ptr<Type> element;
    std::size_t array_count = 0;
    std::size_t size = 0;
    std::size_t align = 1;

    static Type primitive(TypeKind kind);
    static Type pointer(Type pointee);
    static Type array(Type element, std::size_t count);
    std::string str() const;
};

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct IntegerExpr { std::int64_t value = 0; };
struct VariableExpr { std::string name; };
struct BinaryExpr { std::string op; ExprPtr lhs; ExprPtr rhs; };
struct UnaryExpr { std::string op; ExprPtr expr; };
struct CallExpr { std::string callee; std::vector<ExprPtr> args; };

struct Expr {
    std::variant<IntegerExpr, VariableExpr, BinaryExpr, UnaryExpr, CallExpr> node;
    Type type = Type::primitive(TypeKind::Int);
};

struct VarDecl { Type type; std::string name; ExprPtr init; };
struct ReturnStmt { ExprPtr value; };
struct ExprStmt { ExprPtr expr; };
struct CompoundStmt { std::vector<StmtPtr> statements; };
struct IfStmt { ExprPtr condition; StmtPtr then_branch; StmtPtr else_branch; };
struct WhileStmt { ExprPtr condition; StmtPtr body; };
struct ForStmt { StmtPtr init; ExprPtr condition; ExprPtr increment; StmtPtr body; };

struct Stmt {
    std::variant<VarDecl, ReturnStmt, ExprStmt, CompoundStmt, IfStmt, WhileStmt, ForStmt> node;
};

struct Parameter { Type type; std::string name; };
struct FunctionDecl { Type return_type; std::string name; std::vector<Parameter> params; std::unique_ptr<CompoundStmt> body; };
struct StructDecl { std::string name; std::vector<VarDecl> fields; };
struct TranslationUnit { std::vector<StructDecl> structs; std::vector<FunctionDecl> functions; };

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    TranslationUnit parse_translation_unit();
private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    const Token& peek(std::size_t offset = 0) const;
    bool match(TokenKind kind);
    Token expect(TokenKind kind, const std::string& message);
    bool at_type() const;
    Type parse_type();
    FunctionDecl parse_function(Type return_type, std::string name);
    std::unique_ptr<CompoundStmt> parse_compound();
    StmtPtr parse_statement();
    VarDecl parse_var_decl_after_type(Type type);
    ExprPtr parse_expression();
    ExprPtr parse_assignment();
    ExprPtr parse_equality();
    ExprPtr parse_relational();
    ExprPtr parse_additive();
    ExprPtr parse_multiplicative();
    ExprPtr parse_unary();
    ExprPtr parse_primary();
};

enum class IROp { Label, Mov, Load, Store, Add, Sub, Mul, Div, Mod, Cmp, Jmp, Jcc, Call, Ret };

struct IRInst {
    IROp op = IROp::Mov;
    std::string dst;
    std::string a;
    std::string b;
    std::string label;
};

struct IRFunction {
    std::string name;
    Type return_type;
    std::vector<Parameter> params;
    std::vector<IRInst> code;
    std::unordered_map<std::string, int> locals;
};

struct IRModule { std::vector<IRFunction> functions; };

class IRBuilder {
public:
    IRModule build(const TranslationUnit& unit);
private:
    int temp_ = 0;
    int label_ = 0;
    IRFunction* current_ = nullptr;
    std::string temp();
    std::string label(const std::string& prefix);
    std::string emit_expr(const Expr& expr);
    void emit_stmt(const Stmt& stmt);
};

class Optimizer {
public:
    void optimize(IRModule& module);
private:
    void fold_constants(IRFunction& fn);
    void remove_dead_after_ret(IRFunction& fn);
    void simplify_jumps(IRFunction& fn);
};

class AsmGenerator486 {
public:
    std::string generate(const IRModule& module) const;
private:
    std::string emit_function(const IRFunction& fn) const;
};

struct CompileResult {
    TranslationUnit ast;
    IRModule ir;
    std::string assembly;
};

class Compiler486CC {
public:
    CompileResult compile_to_asm(const std::string& source);
};

struct AsmSymbol { std::string name; std::uint32_t address = 0; bool global = false; };
struct AsmRelocation { std::uint32_t offset = 0; std::string symbol; std::int32_t addend = 0; std::uint8_t size = 4; bool relative = false; };
struct ObjectSection { std::string name = ".text"; std::vector<std::uint8_t> data; std::uint32_t base = 0; };
struct ObjectFile { std::vector<ObjectSection> sections; std::vector<AsmSymbol> symbols; std::vector<AsmRelocation> relocations; };

class Assembler486 {
public:
    ObjectFile assemble(const std::string& source) const;
    std::vector<std::uint8_t> assemble_flat(const std::string& source, std::uint32_t base = 0x7C00) const;
};

class Linker486 {
public:
    std::vector<std::uint8_t> link_flat(const std::vector<ObjectFile>& objects, std::uint32_t base = 0x7C00) const;
};

class Toolchain486 {
public:
    CompileResult compile_c486_to_asm(const std::string& source) const;
    ObjectFile assemble(const std::string& assembly) const;
    std::vector<std::uint8_t> link_flat(const std::vector<ObjectFile>& objects, std::uint32_t base = 0x7C00) const;
    std::vector<std::uint8_t> build_c486_flat(const std::string& source, std::uint32_t base = 0x7C00) const;
};

} // namespace c486cc
