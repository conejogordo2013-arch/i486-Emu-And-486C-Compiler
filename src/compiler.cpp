#include "c486cc/compiler.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace c486cc {
namespace {
TokenKind keyword_kind(const std::string& text) {
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"int", TokenKind::KwInt}, {"char", TokenKind::KwChar}, {"float", TokenKind::KwFloat},
        {"double", TokenKind::KwDouble}, {"bool", TokenKind::KwBool}, {"void", TokenKind::KwVoid},
        {"struct", TokenKind::KwStruct}, {"if", TokenKind::KwIf}, {"else", TokenKind::KwElse},
        {"while", TokenKind::KwWhile}, {"for", TokenKind::KwFor}, {"switch", TokenKind::KwSwitch},
        {"case", TokenKind::KwCase}, {"default", TokenKind::KwDefault}, {"return", TokenKind::KwReturn},
    };
    const auto it = keywords.find(text);
    return it == keywords.end() ? TokenKind::Identifier : it->second;
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    std::size_t start = (s[0] == '-') ? 1 : 0;
    if (start == s.size()) return false;
    return std::all_of(s.begin() + static_cast<std::ptrdiff_t>(start), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

ExprPtr make_int(std::int64_t value) {
    auto expr = std::make_unique<Expr>();
    expr->node = IntegerExpr{value};
    return expr;
}

ExprPtr make_var(std::string name) {
    auto expr = std::make_unique<Expr>();
    expr->node = VariableExpr{std::move(name)};
    return expr;
}

ExprPtr make_binary(std::string op, ExprPtr lhs, ExprPtr rhs) {
    auto expr = std::make_unique<Expr>();
    expr->node = BinaryExpr{std::move(op), std::move(lhs), std::move(rhs)};
    return expr;
}

template <typename Node>
StmtPtr make_stmt(Node node) {
    auto stmt = std::make_unique<Stmt>();
    stmt->node = std::move(node);
    return stmt;
}
} // namespace

Lexer::Lexer(std::string source) : source_(std::move(source)) {}
char Lexer::peek(std::size_t offset) const { return pos_ + offset < source_.size() ? source_[pos_ + offset] : '\0'; }
char Lexer::get() {
    const char c = peek();
    if (!c) return '\0';
    ++pos_;
    if (c == '\n') { ++loc_.line; loc_.column = 1; } else { ++loc_.column; }
    return c;
}
void Lexer::skip_space_and_comments() {
    for (;;) {
        while (std::isspace(static_cast<unsigned char>(peek()))) get();
        if (peek() == '/' && peek(1) == '/') { while (peek() && peek() != '\n') get(); continue; }
        if (peek() == '/' && peek(1) == '*') { get(); get(); while (peek() && !(peek() == '*' && peek(1) == '/')) get(); if (peek()) { get(); get(); } continue; }
        break;
    }
}
Token Lexer::next() {
    skip_space_and_comments();
    const auto start = loc_;
    const char c = peek();
    if (!c) return {TokenKind::End, "", start};
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string text;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') text.push_back(get());
        return {keyword_kind(text), text, start};
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        std::string text;
        bool has_dot = false;
        while (std::isdigit(static_cast<unsigned char>(peek())) || (!has_dot && peek() == '.')) {
            if (peek() == '.') has_dot = true;
            text.push_back(get());
        }
        return {has_dot ? TokenKind::FloatLiteral : TokenKind::Integer, text, start};
    }
    if (c == '"') {
        get(); std::string text;
        while (peek() && peek() != '"') text.push_back(get());
        if (peek() == '"') get();
        return {TokenKind::StringLiteral, text, start};
    }
    if (c == '\'') {
        get(); char value = get(); if (peek() == '\'') get();
        return {TokenKind::CharLiteral, std::string(1, value), start};
    }
    auto two = std::string{c, peek(1)};
    static const std::unordered_map<std::string, TokenKind> two_tokens = {
        {"==", TokenKind::Eq}, {"!=", TokenKind::Ne}, {"<=", TokenKind::Le}, {">=", TokenKind::Ge},
        {"&&", TokenKind::AndAnd}, {"||", TokenKind::OrOr}, {"->", TokenKind::Arrow},
    };
    const auto it2 = two_tokens.find(two);
    if (it2 != two_tokens.end()) { get(); get(); return {it2->second, two, start}; }
    const char one = get();
    switch (one) {
    case '+': return {TokenKind::Plus, "+", start}; case '-': return {TokenKind::Minus, "-", start};
    case '*': return {TokenKind::Star, "*", start}; case '/': return {TokenKind::Slash, "/", start};
    case '%': return {TokenKind::Percent, "%", start}; case '&': return {TokenKind::Amp, "&", start};
    case '|': return {TokenKind::Pipe, "|", start}; case '^': return {TokenKind::Caret, "^", start};
    case '!': return {TokenKind::Bang, "!", start}; case '~': return {TokenKind::Tilde, "~", start};
    case '=': return {TokenKind::Assign, "=", start}; case '<': return {TokenKind::Lt, "<", start};
    case '>': return {TokenKind::Gt, ">", start}; case '(': return {TokenKind::LParen, "(", start};
    case ')': return {TokenKind::RParen, ")", start}; case '{': return {TokenKind::LBrace, "{", start};
    case '}': return {TokenKind::RBrace, "}", start}; case '[': return {TokenKind::LBracket, "[", start};
    case ']': return {TokenKind::RBracket, "]", start}; case ',': return {TokenKind::Comma, ",", start};
    case ';': return {TokenKind::Semicolon, ";", start}; case ':': return {TokenKind::Colon, ":", start};
    case '.': return {TokenKind::Dot, ".", start};
    default: throw CompileError("unexpected character in lexer");
    }
}
std::vector<Token> Lexer::all() { std::vector<Token> tokens; for (;;) { auto t = next(); tokens.push_back(t); if (t.kind == TokenKind::End) break; } return tokens; }

Type Type::primitive(TypeKind kind) {
    switch (kind) {
    case TypeKind::Void: return {kind, "void", nullptr, 0, 0, 1};
    case TypeKind::Bool: return {kind, "bool", nullptr, 0, 1, 1};
    case TypeKind::Char: return {kind, "char", nullptr, 0, 1, 1};
    case TypeKind::Int: return {kind, "int", nullptr, 0, 4, 4};
    case TypeKind::Float: return {kind, "float", nullptr, 0, 4, 4};
    case TypeKind::Double: return {kind, "double", nullptr, 0, 8, 4};
    default: return {kind, "", nullptr, 0, 0, 1};
    }
}
Type Type::pointer(Type pointee) { return {TypeKind::Pointer, pointee.str() + "*", std::make_shared<Type>(std::move(pointee)), 0, 4, 4}; }
Type Type::array(Type elem, std::size_t count) { auto size = elem.size * count; return {TypeKind::Array, elem.str() + "[]", std::make_shared<Type>(std::move(elem)), count, size, 4}; }
std::string Type::str() const { return name.empty() ? "type" : name; }

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
const Token& Parser::peek(std::size_t offset) const { return tokens_[std::min(pos_ + offset, tokens_.size() - 1)]; }
bool Parser::match(TokenKind kind) { if (peek().kind == kind) { ++pos_; return true; } return false; }
Token Parser::expect(TokenKind kind, const std::string& message) { if (peek().kind != kind) throw CompileError(message); return tokens_[pos_++]; }
bool Parser::at_type() const { auto k = peek().kind; return k == TokenKind::KwInt || k == TokenKind::KwChar || k == TokenKind::KwFloat || k == TokenKind::KwDouble || k == TokenKind::KwBool || k == TokenKind::KwVoid || k == TokenKind::KwStruct; }
Type Parser::parse_type() {
    Type type;
    if (match(TokenKind::KwInt)) type = Type::primitive(TypeKind::Int);
    else if (match(TokenKind::KwChar)) type = Type::primitive(TypeKind::Char);
    else if (match(TokenKind::KwFloat)) type = Type::primitive(TypeKind::Float);
    else if (match(TokenKind::KwDouble)) type = Type::primitive(TypeKind::Double);
    else if (match(TokenKind::KwBool)) type = Type::primitive(TypeKind::Bool);
    else if (match(TokenKind::KwVoid)) type = Type::primitive(TypeKind::Void);
    else if (match(TokenKind::KwStruct)) { auto name = expect(TokenKind::Identifier, "expected struct name").text; type = {TypeKind::Struct, "struct " + name, nullptr, 0, 0, 4}; }
    else throw CompileError("expected type");
    while (match(TokenKind::Star)) type = Type::pointer(std::move(type));
    return type;
}
TranslationUnit Parser::parse_translation_unit() {
    TranslationUnit unit;
    while (peek().kind != TokenKind::End) {
        Type type = parse_type();
        std::string name = expect(TokenKind::Identifier, "expected declaration name").text;
        if (peek().kind == TokenKind::LParen) unit.functions.push_back(parse_function(std::move(type), std::move(name)));
        else throw CompileError("only function definitions are supported by current 486CC parser core");
    }
    return unit;
}
FunctionDecl Parser::parse_function(Type return_type, std::string name) {
    expect(TokenKind::LParen, "expected '('");
    std::vector<Parameter> params;
    if (!match(TokenKind::RParen)) {
        do { Type pt = parse_type(); std::string pn = expect(TokenKind::Identifier, "expected parameter name").text; params.push_back({std::move(pt), std::move(pn)}); } while (match(TokenKind::Comma));
        expect(TokenKind::RParen, "expected ')'");
    }
    return {std::move(return_type), std::move(name), std::move(params), parse_compound()};
}
std::unique_ptr<CompoundStmt> Parser::parse_compound() {
    expect(TokenKind::LBrace, "expected '{'");
    auto block = std::make_unique<CompoundStmt>();
    while (!match(TokenKind::RBrace)) block->statements.push_back(parse_statement());
    return block;
}
StmtPtr Parser::parse_statement() {
    if (match(TokenKind::KwReturn)) { auto value = parse_expression(); expect(TokenKind::Semicolon, "expected ';'"); return make_stmt(ReturnStmt{std::move(value)}); }
    if (match(TokenKind::KwIf)) { expect(TokenKind::LParen, "expected '('"); auto cond = parse_expression(); expect(TokenKind::RParen, "expected ')'"); auto then_s = parse_statement(); StmtPtr else_s; if (match(TokenKind::KwElse)) else_s = parse_statement(); return make_stmt(IfStmt{std::move(cond), std::move(then_s), std::move(else_s)}); }
    if (match(TokenKind::KwWhile)) { expect(TokenKind::LParen, "expected '('"); auto cond = parse_expression(); expect(TokenKind::RParen, "expected ')'"); return make_stmt(WhileStmt{std::move(cond), parse_statement()}); }
    if (peek().kind == TokenKind::LBrace) { auto compound = parse_compound(); return make_stmt(CompoundStmt{std::move(compound->statements)}); }
    if (at_type()) { Type type = parse_type(); auto decl = parse_var_decl_after_type(std::move(type)); expect(TokenKind::Semicolon, "expected ';'"); return make_stmt(std::move(decl)); }
    auto expr = parse_expression(); expect(TokenKind::Semicolon, "expected ';'"); return make_stmt(ExprStmt{std::move(expr)});
}
VarDecl Parser::parse_var_decl_after_type(Type type) { std::string name = expect(TokenKind::Identifier, "expected variable name").text; ExprPtr init; if (match(TokenKind::Assign)) init = parse_expression(); return {std::move(type), std::move(name), std::move(init)}; }
ExprPtr Parser::parse_expression() { return parse_assignment(); }
ExprPtr Parser::parse_assignment() { auto lhs = parse_equality(); if (match(TokenKind::Assign)) return make_binary("=", std::move(lhs), parse_assignment()); return lhs; }
ExprPtr Parser::parse_equality() { auto e = parse_relational(); while (peek().kind == TokenKind::Eq || peek().kind == TokenKind::Ne) { std::string op = match(TokenKind::Eq) ? "==" : (expect(TokenKind::Ne, "").text); e = make_binary(op, std::move(e), parse_relational()); } return e; }
ExprPtr Parser::parse_relational() { auto e = parse_additive(); while (peek().kind == TokenKind::Lt || peek().kind == TokenKind::Le || peek().kind == TokenKind::Gt || peek().kind == TokenKind::Ge) { std::string op = peek().text; ++pos_; e = make_binary(op, std::move(e), parse_additive()); } return e; }
ExprPtr Parser::parse_additive() { auto e = parse_multiplicative(); while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) { std::string op = peek().text; ++pos_; e = make_binary(op, std::move(e), parse_multiplicative()); } return e; }
ExprPtr Parser::parse_multiplicative() { auto e = parse_unary(); while (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash || peek().kind == TokenKind::Percent) { std::string op = peek().text; ++pos_; e = make_binary(op, std::move(e), parse_unary()); } return e; }
ExprPtr Parser::parse_unary() { if (peek().kind == TokenKind::Minus || peek().kind == TokenKind::Bang || peek().kind == TokenKind::Star || peek().kind == TokenKind::Amp) { std::string op = peek().text; ++pos_; auto e = std::make_unique<Expr>(); e->node = UnaryExpr{op, parse_unary()}; return e; } return parse_primary(); }
ExprPtr Parser::parse_primary() {
    if (peek().kind == TokenKind::Integer) return make_int(std::stoll(expect(TokenKind::Integer, "").text));
    if (peek().kind == TokenKind::CharLiteral) return make_int(expect(TokenKind::CharLiteral, "").text[0]);
    if (peek().kind == TokenKind::Identifier) {
        std::string name = expect(TokenKind::Identifier, "").text;
        if (match(TokenKind::LParen)) {
            auto e = std::make_unique<Expr>(); CallExpr call; call.callee = std::move(name);
            if (!match(TokenKind::RParen)) { do { call.args.push_back(parse_expression()); } while (match(TokenKind::Comma)); expect(TokenKind::RParen, "expected ')'"); }
            e->node = std::move(call); return e;
        }
        return make_var(std::move(name));
    }
    if (match(TokenKind::LParen)) { auto e = parse_expression(); expect(TokenKind::RParen, "expected ')'"); return e; }
    throw CompileError("expected expression");
}

std::string IRBuilder::temp() { return "%t" + std::to_string(temp_++); }
std::string IRBuilder::label(const std::string& prefix) { return ".L" + prefix + std::to_string(label_++); }
IRModule IRBuilder::build(const TranslationUnit& unit) {
    IRModule module;
    for (const auto& fn : unit.functions) {
        IRFunction irfn; irfn.name = fn.name; irfn.return_type = fn.return_type; irfn.params = fn.params; current_ = &irfn; temp_ = 0;
        Stmt wrapper; wrapper.node = CompoundStmt{};
        for (const auto& stmt : fn.body->statements) emit_stmt(*stmt);
        if (irfn.code.empty() || irfn.code.back().op != IROp::Ret) irfn.code.push_back({IROp::Ret, "0"});
        module.functions.push_back(std::move(irfn));
    }
    return module;
}
std::string IRBuilder::emit_expr(const Expr& expr) {
    if (auto n = std::get_if<IntegerExpr>(&expr.node)) return std::to_string(n->value);
    if (auto n = std::get_if<VariableExpr>(&expr.node)) return n->name;
    if (auto n = std::get_if<UnaryExpr>(&expr.node)) { auto v = emit_expr(*n->expr); if (n->op == "-") { auto out = temp(); current_->code.push_back({IROp::Sub, out, "0", v}); return out; } return v; }
    if (auto n = std::get_if<BinaryExpr>(&expr.node)) {
        if (n->op == "=") { auto rhs = emit_expr(*n->rhs); auto* var = std::get_if<VariableExpr>(&n->lhs->node); if (!var) throw CompileError("assignment target must be a variable in current IR core"); current_->code.push_back({IROp::Mov, var->name, rhs}); return var->name; }
        auto lhs = emit_expr(*n->lhs); auto rhs = emit_expr(*n->rhs); auto out = temp();
        IROp op = n->op == "+" ? IROp::Add : n->op == "-" ? IROp::Sub : n->op == "*" ? IROp::Mul : n->op == "/" ? IROp::Div : n->op == "%" ? IROp::Mod : IROp::Cmp;
        current_->code.push_back({op, out, lhs, rhs, n->op}); return out;
    }
    if (auto n = std::get_if<CallExpr>(&expr.node)) { for (auto it = n->args.rbegin(); it != n->args.rend(); ++it) current_->code.push_back({IROp::Mov, "push", emit_expr(**it)}); auto out = temp(); current_->code.push_back({IROp::Call, out, n->callee}); return out; }
    throw CompileError("unknown expression");
}
void IRBuilder::emit_stmt(const Stmt& stmt) {
    if (auto n = std::get_if<VarDecl>(&stmt.node)) { current_->locals.emplace(n->name, -4 * (static_cast<int>(current_->locals.size()) + 1)); if (n->init) current_->code.push_back({IROp::Mov, n->name, emit_expr(*n->init)}); return; }
    if (auto n = std::get_if<ReturnStmt>(&stmt.node)) { current_->code.push_back({IROp::Ret, emit_expr(*n->value)}); return; }
    if (auto n = std::get_if<ExprStmt>(&stmt.node)) { emit_expr(*n->expr); return; }
    if (auto n = std::get_if<CompoundStmt>(&stmt.node)) { for (const auto& s : n->statements) emit_stmt(*s); return; }
    if (auto n = std::get_if<IfStmt>(&stmt.node)) { auto else_l = label("else"); auto end_l = label("endif"); auto c = emit_expr(*n->condition); current_->code.push_back({IROp::Jcc, "z", c, "0", else_l}); emit_stmt(*n->then_branch); current_->code.push_back({IROp::Jmp, "", "", "", end_l}); current_->code.push_back({IROp::Label, "", "", "", else_l}); if (n->else_branch) emit_stmt(*n->else_branch); current_->code.push_back({IROp::Label, "", "", "", end_l}); return; }
    if (auto n = std::get_if<WhileStmt>(&stmt.node)) { auto start = label("while"); auto end = label("endwhile"); current_->code.push_back({IROp::Label, "", "", "", start}); auto c = emit_expr(*n->condition); current_->code.push_back({IROp::Jcc, "z", c, "0", end}); emit_stmt(*n->body); current_->code.push_back({IROp::Jmp, "", "", "", start}); current_->code.push_back({IROp::Label, "", "", "", end}); }
}

void Optimizer::optimize(IRModule& module) { for (auto& fn : module.functions) { fold_constants(fn); remove_dead_after_ret(fn); simplify_jumps(fn); } }
void Optimizer::fold_constants(IRFunction& fn) {
    for (auto& i : fn.code) {
        if ((i.op == IROp::Add || i.op == IROp::Sub || i.op == IROp::Mul || i.op == IROp::Div || i.op == IROp::Mod || i.op == IROp::Cmp) && is_number(i.a) && is_number(i.b)) {
            auto a = std::stoll(i.a), b = std::stoll(i.b), r = 0LL;
            if (i.op == IROp::Add) r = a + b; else if (i.op == IROp::Sub) r = a - b; else if (i.op == IROp::Mul) r = a * b; else if (i.op == IROp::Div) r = b ? a / b : 0; else if (i.op == IROp::Mod) r = b ? a % b : 0; else if (i.label == "==") r = a == b; else if (i.label == "!=") r = a != b; else if (i.label == "<") r = a < b; else if (i.label == "<=") r = a <= b; else if (i.label == ">") r = a > b; else if (i.label == ">=") r = a >= b;
            i.op = IROp::Mov; i.a = std::to_string(r); i.b.clear();
        }
    }
}
void Optimizer::remove_dead_after_ret(IRFunction& fn) { bool dead = false; std::vector<IRInst> out; for (auto& i : fn.code) { if (dead && i.op != IROp::Label) continue; out.push_back(i); dead = i.op == IROp::Ret; } fn.code = std::move(out); }
void Optimizer::simplify_jumps(IRFunction& fn) {
    std::vector<IRInst> out;
    for (std::size_t i = 0; i < fn.code.size(); ++i) {
        if (i + 1 < fn.code.size() && fn.code[i].op == IROp::Jmp && fn.code[i + 1].op == IROp::Label && fn.code[i].label == fn.code[i + 1].label) continue;
        out.push_back(fn.code[i]);
    }
    fn.code = std::move(out);
}

std::string AsmGenerator486::generate(const IRModule& module) const { std::ostringstream out; out << "bits 32\nsection .text\n"; for (const auto& fn : module.functions) out << emit_function(fn); return out.str(); }
std::string AsmGenerator486::emit_function(const IRFunction& fn) const {
    std::ostringstream out; out << "global " << fn.name << "\n" << fn.name << ":\n  push ebp\n  mov ebp, esp\n";
    auto slots = fn.locals;
    auto ensure_slot = [&](const std::string& v) {
        if (v.empty() || is_number(v) || v == "push") return;
        if (slots.find(v) == slots.end() && v.rfind("%t", 0) == 0) slots[v] = -4 * (static_cast<int>(slots.size()) + 1);
    };
    for (const auto& i : fn.code) { ensure_slot(i.dst); ensure_slot(i.a); ensure_slot(i.b); }
    const int frame = static_cast<int>(slots.size()) * 4; if (frame) out << "  sub esp, " << frame << "\n";
    auto operand = [&](const std::string& v) {
        auto it = slots.find(v); if (it != slots.end()) return std::string("[ebp") + std::to_string(it->second) + "]";
        for (std::size_t idx = 0; idx < fn.params.size(); ++idx) if (fn.params[idx].name == v) return std::string("[ebp+") + std::to_string(8 + static_cast<int>(idx) * 4) + "]";
        return v;
    };
    auto setcc = [](const std::string& op) { if (op == "!=") return "setne"; if (op == "<") return "setl"; if (op == "<=") return "setle"; if (op == ">") return "setg"; if (op == ">=") return "setge"; return "sete"; };
    for (const auto& i : fn.code) {
        switch (i.op) {
        case IROp::Label: out << i.label << ":\n"; break;
        case IROp::Mov: if (i.dst == "push") out << "  push dword " << operand(i.a) << "\n"; else out << "  mov eax, " << operand(i.a) << "\n  mov " << operand(i.dst) << ", eax\n"; break;
        case IROp::Add: out << "  mov eax, " << operand(i.a) << "\n  add eax, " << operand(i.b) << "\n  mov " << operand(i.dst) << ", eax\n"; break;
        case IROp::Sub: out << "  mov eax, " << operand(i.a) << "\n  sub eax, " << operand(i.b) << "\n  mov " << operand(i.dst) << ", eax\n"; break;
        case IROp::Mul: out << "  mov eax, " << operand(i.a) << "\n  imul eax, " << operand(i.b) << "\n  mov " << operand(i.dst) << ", eax\n"; break;
        case IROp::Div:
            out << "  mov eax, " << operand(i.a) << "\n  cdq\n";
            if (is_number(i.b)) out << "  mov ecx, " << i.b << "\n  idiv ecx\n"; else out << "  idiv dword " << operand(i.b) << "\n";
            out << "  mov " << operand(i.dst) << ", eax\n";
            break;
        case IROp::Mod:
            out << "  mov eax, " << operand(i.a) << "\n  cdq\n";
            if (is_number(i.b)) out << "  mov ecx, " << i.b << "\n  idiv ecx\n"; else out << "  idiv dword " << operand(i.b) << "\n";
            out << "  mov " << operand(i.dst) << ", edx\n";
            break;
        case IROp::Cmp: out << "  mov eax, " << operand(i.a) << "\n  cmp eax, " << operand(i.b) << "\n  " << setcc(i.label) << " al\n  movzx eax, al\n  mov " << operand(i.dst) << ", eax\n"; break;
        case IROp::Jmp: out << "  jmp " << i.label << "\n"; break;
        case IROp::Jcc: out << "  cmp " << operand(i.a) << ", " << operand(i.b) << "\n  je " << i.label << "\n"; break;
        case IROp::Call: out << "  call " << i.a << "\n  mov " << operand(i.dst) << ", eax\n"; break;
        case IROp::Ret: out << "  mov eax, " << operand(i.dst) << "\n  mov esp, ebp\n  pop ebp\n  ret\n"; break;
        default: break;
        }
    }
    return out.str();
}

CompileResult Compiler486CC::compile_to_asm(const std::string& source) {
    Lexer lexer(source); Parser parser(lexer.all()); auto ast = parser.parse_translation_unit(); IRBuilder builder; auto ir = builder.build(ast); Optimizer opt; opt.optimize(ir); AsmGenerator486 backend; auto asm_text = backend.generate(ir); return {std::move(ast), std::move(ir), std::move(asm_text)};
}

} // namespace c486cc
