grammar MiniC;

@header {
#include <vector>
#include "Program.h"
#include "Declarations.h"
#include "Statements.h"
#include "Exprs.h"
#include "Terms.h"
}

/* Original: prog : preamble decl* EOF ; */
prog returns [minicc::Program *val]
@init { $val = new minicc::Program(); }
:   preamble { $val->setSyslibFlag($preamble.val); }
    (d=decl { $val->addChild($d.val); })*
    EOF
    { $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: preamble : ('#include' '"minicio.h"')? ; */
preamble returns [bool val]
@init { $val = false; }
:   ('#include' '"minicio.h"' { $val = true; })?
;

/* Original: decl : vardecl | funcdecl ; */
decl returns [minicc::Declaration *val]
:   vardecl  { $val = $vardecl.val; }
|   funcdecl { $val = $funcdecl.val; }
;

/* Original: vardecl : vartype varlist ';' ; */
vardecl returns [minicc::VarDeclaration *val]
@init { $val = new minicc::VarDeclaration(); }
:   vartype { $val->addChild($vartype.val); }
    varlist { for (auto v : $varlist.val) { $val->addChild(v); } }
    ';'
    { $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original:
funcdecl
:   rettype funcname '(' parameters ')' scope
|   rettype funcname '(' parameters ')' ';'
;
*/
funcdecl returns [minicc::FuncDeclaration *val]
@init { $val = new minicc::FuncDeclaration(); }
:   rettype { $val->addChild($rettype.val); }
    funcname { $val->addChild($funcname.val); }
    '(' parameters ')' { for (auto p : $parameters.val) { $val->addChild(p); } }
    scope
    {
        $val->setHasBody(true);
        $val->addChild($scope.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   rettype { $val->addChild($rettype.val); }
    funcname { $val->addChild($funcname.val); }
    '(' parameters ')' { for (auto p : $parameters.val) { $val->addChild(p); } }
    ';'
    {
        $val->setHasBody(false);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
;

/* Original: scope : '{' vardecl* stmt* '}' ; */
scope returns [minicc::ScopeStatement *val] locals [size_t numVarDecl]
@init { $val = new minicc::ScopeStatement(); $numVarDecl = 0; }
:   '{'
    (v=vardecl { $val->addChild($v.val); $numVarDecl++; })*
    (s=stmt { $val->addChild($s.val); })*
    '}'
    {
        $val->setNumVarDecl($numVarDecl);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
;

/* Original:
stmt
:   expr ';'
|   'if' '(' expr ')' stmt ('else' stmt)?
|   'for' '(' expropt ';' expropt ';' expropt ')' stmt
|   'while' '(' expr ')' stmt
|   'break' ';'
|   'continue' ';'
|   'return' expropt ';'
|   scope
;
*/
stmt returns [minicc::Statement *val]
:   expr ';'
    {
        $val = new minicc::ExprStatement();
        $val->addChild($expr.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'if' '(' ifCond=expr ')' thenStmt=stmt (elseKw='else' elseStmt=stmt)?
    {
        $val = new minicc::IfStatement();
        $val->addChild($ifCond.val);
        $val->addChild($thenStmt.val);
        if ($elseKw != nullptr) {
            $val->addChild($elseStmt.val);
        }
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'for' '(' initExpr=expropt ';' condExpr=expropt ';' iterExpr=expropt ')' forBody=stmt
    {
        $val = new minicc::ForStatement();
        $val->addChild($initExpr.val);
        $val->addChild($condExpr.val);
        $val->addChild($iterExpr.val);
        $val->addChild($forBody.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'while' '(' whileCond=expr ')' whileBody=stmt
    {
        $val = new minicc::WhileStatement();
        $val->addChild($whileCond.val);
        $val->addChild($whileBody.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'break' ';'
    {
        $val = new minicc::BreakStatement();
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'continue' ';'
    {
        $val = new minicc::ContinueStatement();
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'return' retExpr=expropt ';'
    {
        $val = new minicc::ReturnStatement();
        if ($retExpr.val != nullptr) {
            $val->addChild($retExpr.val);
        }
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   scope
    {
        $val = $scope.val;
    }
;

/* Original: varlist : varlistentry (',' varlistentry)* ; */
varlist returns [std::vector<minicc::VarReference*> val]
@init { $val = std::vector<minicc::VarReference*>(); }
:   v1=varlistentry { $val.push_back($v1.val); }
    (',' v2=varlistentry { $val.push_back($v2.val); })*
;

/* Original: varlistentry : varname ('[' INT ']')? ; */
varlistentry returns [minicc::VarReference *val] locals [minicc::ConstantLiteralExpr *tmp]
@init { $val = new minicc::VarReference(); }
:   varname { $val->addChild($varname.val); }
    ('[' INT ']' { 
        $tmp = minicc::ConstantLiteralExpr::fromString($INT.text);
        $tmp->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
        $val->addChild($tmp); 
    })?
    { $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: vartype : 'int' | 'bool' | 'char' ; */
vartype returns [minicc::TypeReference *val]
:   'int'  { $val = new minicc::TypeReference(minicc::Type::Int); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
|   'bool' { $val = new minicc::TypeReference(minicc::Type::Bool); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
|   'char' { $val = new minicc::TypeReference(minicc::Type::Char); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: rettype : 'void' | vartype ; */
rettype returns [minicc::TypeReference *val]
:   'void'   { $val = new minicc::TypeReference(minicc::Type::Void); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
|   vartype  { $val = $vartype.val; }
;

/* Original: parameters : parameterlist? ; */
parameters returns [std::vector<minicc::Parameter*> val]
@init { $val = std::vector<minicc::Parameter*>(); }
:   (parameterlist { $val = $parameterlist.val; })?
;

/* Original: parameterlist : parameterentry (',' parameterentry)* ; */
parameterlist returns [std::vector<minicc::Parameter*> val]
@init { $val = std::vector<minicc::Parameter*>(); }
:   p1=parameterentry { $val.push_back($p1.val); }
    (',' p2=parameterentry { $val.push_back($p2.val); })*
;

/* Original: parameterentry : vartype parametername ; */
parameterentry returns [minicc::Parameter *val]
@init { $val = new minicc::Parameter(); }
:   vartype { $val->addChild($vartype.val); }
    parametername { $val->addChild($parametername.val); }
    { $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: expropt : expr? ; */
expropt returns [minicc::Expr *val]
@init { $val = nullptr; }
:   (expr { $val = $expr.val; })?
;

/* Original:
expr
:   funcname '(' arguments ')'
|   var
|   INT
|   CHAR
|   'true'
|   'false'
|   '(' expr ')'
|   '-' expr
|   '!' expr
|   expr ('*' | '/') expr
|   expr ('+' | '-') expr
|   expr ('==' | '!=' | '<' | '<=' | '>' | '>=') expr
|   expr '&&' expr
|   expr '||' expr
|   <assoc=right> var '=' expr
;
*/
expr returns [minicc::Expr *val]
:   funcname '(' arguments ')'
    {
        $val = new minicc::CallExpr();
        $val->addChild($funcname.val);
        for (auto arg : $arguments.val) { $val->addChild(arg); }
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   var
    {
        $val = new minicc::VarExpr();
        $val->addChild($var.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   INT
    {
        $val = minicc::ConstantLiteralExpr::fromString($INT.text);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   CHAR
    {
        $val = new minicc::CharLiteralExpr($CHAR.text[1]);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'true'
    {
        $val = new minicc::BoolLiteralExpr(true);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   'false'
    {
        $val = new minicc::BoolLiteralExpr(false);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   '(' inner=expr ')'
    {
        $val = $inner.val;
    }
|   '-' unaryExpr=expr
    {
        // Handle INT_MIN case: -2147483648 needs special handling
        if (typeid(*$unaryExpr.val) == typeid(minicc::IntLiteralExpr)) {
            delete $unaryExpr.val;
            $unaryExpr.val = minicc::ConstantLiteralExpr::fromString($unaryExpr.text, true);
            $unaryExpr.val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
        }
        $val = new minicc::UnaryExpr();
        ((minicc::UnaryExpr*)$val)->setOpcode(minicc::Expr::Sub);
        $val->addChild($unaryExpr.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   '!' notExpr=expr
    {
        $val = new minicc::UnaryExpr();
        ((minicc::UnaryExpr*)$val)->setOpcode(minicc::Expr::Not);
        $val->addChild($notExpr.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   left1=expr op1=('*' | '/') right1=expr
    {
        $val = new minicc::BinaryExpr();
        ((minicc::BinaryExpr*)$val)->setOpcode(minicc::Expr::opcodeFromString($op1.text));
        $val->addChild($left1.val);
        $val->addChild($right1.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   left2=expr op2=('+' | '-') right2=expr
    {
        $val = new minicc::BinaryExpr();
        ((minicc::BinaryExpr*)$val)->setOpcode(minicc::Expr::opcodeFromString($op2.text));
        $val->addChild($left2.val);
        $val->addChild($right2.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   left3=expr op3=('==' | '!=' | '<' | '<=' | '>' | '>=') right3=expr
    {
        $val = new minicc::BinaryExpr();
        ((minicc::BinaryExpr*)$val)->setOpcode(minicc::Expr::opcodeFromString($op3.text));
        $val->addChild($left3.val);
        $val->addChild($right3.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   left4=expr '&&' right4=expr
    {
        $val = new minicc::BinaryExpr();
        ((minicc::BinaryExpr*)$val)->setOpcode(minicc::Expr::And);
        $val->addChild($left4.val);
        $val->addChild($right4.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   left5=expr '||' right5=expr
    {
        $val = new minicc::BinaryExpr();
        ((minicc::BinaryExpr*)$val)->setOpcode(minicc::Expr::Or);
        $val->addChild($left5.val);
        $val->addChild($right5.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
|   <assoc=right> assignVar=var '=' assignExpr=expr
    {
        $val = new minicc::AssignmentExpr();
        $val->addChild($assignVar.val);
        $val->addChild($assignExpr.val);
        $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine());
    }
;

/* Original: var : varname ('[' expr ']')? ; */
var returns [minicc::VarReference *val]
@init { $val = new minicc::VarReference(); }
:   varname { $val->addChild($varname.val); }
    ('[' expr ']' { $val->addChild($expr.val); })?
    { $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: arguments : argumentlist? ; */
arguments returns [std::vector<minicc::Expr*> val]
@init { $val = std::vector<minicc::Expr*>(); }
:   (argumentlist { $val = $argumentlist.val; })?
;

/* Original: argumentlist : expr (',' expr)* ; */
argumentlist returns [std::vector<minicc::Expr*> val]
@init { $val = std::vector<minicc::Expr*>(); }
:   e1=expr { $val.push_back($e1.val); }
    (',' e2=expr { $val.push_back($e2.val); })*
;

/* Original: varname : ID ; */
varname returns [minicc::Identifier *val]
:   ID { $val = new minicc::Identifier($ID.text); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: funcname : ID ; */
funcname returns [minicc::Identifier *val]
:   ID { $val = new minicc::Identifier($ID.text); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

/* Original: parametername : ID ; */
parametername returns [minicc::Identifier *val]
:   ID { $val = new minicc::Identifier($ID.text); $val->setSrcLoc($ctx->start->getLine(), $ctx->start->getCharPositionInLine()); }
;

ID:     [a-zA-Z][a-zA-Z0-9_]* ;
INT:    [0] | ([1-9][0-9]*) ;
CHAR:   '\'' . '\'' ;
WS:     [ \t\r\n]+ -> skip;
COMMENT: '//' (~[\r\n])* -> skip;
