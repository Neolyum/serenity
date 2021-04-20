/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Parser.h"
#include <AK/TypeCasts.h>

namespace SQL {

Parser::Parser(Lexer lexer)
    : m_parser_state(move(lexer))
{
}

NonnullRefPtr<Statement> Parser::next_statement()
{
    switch (m_parser_state.m_token.type()) {
    case TokenType::Create:
        return parse_create_table_statement();
    case TokenType::Drop:
        return parse_drop_table_statement();
    default:
        expected("CREATE or DROP");
        return create_ast_node<ErrorStatement>();
    }
}

NonnullRefPtr<CreateTable> Parser::parse_create_table_statement()
{
    // https://sqlite.org/lang_createtable.html
    consume(TokenType::Create);

    bool is_temporary = false;
    if (consume_if(TokenType::Temp) || consume_if(TokenType::Temporary))
        is_temporary = true;

    consume(TokenType::Table);

    bool is_error_if_table_exists = true;
    if (consume_if(TokenType::If)) {
        consume(TokenType::Not);
        consume(TokenType::Exists);
        is_error_if_table_exists = false;
    }

    String schema_or_table_name = consume(TokenType::Identifier).value();
    String schema_name;
    String table_name;

    if (consume_if(TokenType::Period)) {
        schema_name = move(schema_or_table_name);
        table_name = consume(TokenType::Identifier).value();
    } else {
        table_name = move(schema_or_table_name);
    }

    // FIXME: Parse "AS select-stmt".

    NonnullRefPtrVector<ColumnDefinition> column_definitions;
    consume(TokenType::ParenOpen);
    do {
        column_definitions.append(parse_column_definition());

        if (match(TokenType::ParenClose))
            break;

        consume(TokenType::Comma);
    } while (!match(TokenType::Eof));

    // FIXME: Parse "table-constraint".

    consume(TokenType::ParenClose);
    consume(TokenType::SemiColon);

    return create_ast_node<CreateTable>(move(schema_name), move(table_name), move(column_definitions), is_temporary, is_error_if_table_exists);
}

NonnullRefPtr<DropTable> Parser::parse_drop_table_statement()
{
    // https://sqlite.org/lang_droptable.html
    consume(TokenType::Drop);
    consume(TokenType::Table);

    bool is_error_if_table_does_not_exist = true;
    if (consume_if(TokenType::If)) {
        consume(TokenType::Exists);
        is_error_if_table_does_not_exist = false;
    }

    String schema_or_table_name = consume(TokenType::Identifier).value();
    String schema_name;
    String table_name;

    if (consume_if(TokenType::Period)) {
        schema_name = move(schema_or_table_name);
        table_name = consume(TokenType::Identifier).value();
    } else {
        table_name = move(schema_or_table_name);
    }

    consume(TokenType::SemiColon);

    return create_ast_node<DropTable>(move(schema_name), move(table_name), is_error_if_table_does_not_exist);
}

NonnullRefPtr<Expression> Parser::parse_expression()
{
    // https://sqlite.org/lang_expr.html
    auto expression = parse_primary_expression();

    if (match_secondary_expression())
        expression = parse_secondary_expression(move(expression));

    // FIXME: Parse 'bind-parameter'.
    // FIXME: Parse 'function-name'.
    // FIXME: Parse 'exists'.
    // FIXME: Parse 'raise-function'.

    return expression;
}

NonnullRefPtr<Expression> Parser::parse_primary_expression()
{
    if (auto expression = parse_literal_value_expression(); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_column_name_expression(); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_unary_operator_expression(); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_chained_expression(); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_cast_expression(); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_case_expression(); expression.has_value())
        return move(expression.value());

    expected("Primary Expression");
    consume();

    return create_ast_node<ErrorExpression>();
}

NonnullRefPtr<Expression> Parser::parse_secondary_expression(NonnullRefPtr<Expression> primary)
{
    if (auto expression = parse_binary_operator_expression(primary); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_collate_expression(primary); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_is_expression(primary); expression.has_value())
        return move(expression.value());

    bool invert_expression = false;
    if (consume_if(TokenType::Not))
        invert_expression = true;

    if (auto expression = parse_match_expression(primary, invert_expression); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_null_expression(primary, invert_expression); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_between_expression(primary, invert_expression); expression.has_value())
        return move(expression.value());

    if (auto expression = parse_in_expression(primary, invert_expression); expression.has_value())
        return move(expression.value());

    expected("Secondary Expression");
    consume();

    return create_ast_node<ErrorExpression>();
}

bool Parser::match_secondary_expression() const
{
    return match(TokenType::Not)
        || match(TokenType::DoublePipe)
        || match(TokenType::Asterisk)
        || match(TokenType::Divide)
        || match(TokenType::Modulus)
        || match(TokenType::Plus)
        || match(TokenType::Minus)
        || match(TokenType::ShiftLeft)
        || match(TokenType::ShiftRight)
        || match(TokenType::Ampersand)
        || match(TokenType::Pipe)
        || match(TokenType::LessThan)
        || match(TokenType::LessThanEquals)
        || match(TokenType::GreaterThan)
        || match(TokenType::GreaterThanEquals)
        || match(TokenType::Equals)
        || match(TokenType::EqualsEquals)
        || match(TokenType::NotEquals1)
        || match(TokenType::NotEquals2)
        || match(TokenType::And)
        || match(TokenType::Or)
        || match(TokenType::Collate)
        || match(TokenType::Is)
        || match(TokenType::Like)
        || match(TokenType::Glob)
        || match(TokenType::Match)
        || match(TokenType::Regexp)
        || match(TokenType::Isnull)
        || match(TokenType::Notnull)
        || match(TokenType::Between)
        || match(TokenType::In);
}

Optional<NonnullRefPtr<Expression>> Parser::parse_literal_value_expression()
{
    if (match(TokenType::NumericLiteral)) {
        auto value = consume().double_value();
        return create_ast_node<NumericLiteral>(value);
    }
    if (match(TokenType::StringLiteral)) {
        // TODO: Should the surrounding ' ' be removed here?
        auto value = consume().value();
        return create_ast_node<StringLiteral>(value);
    }
    if (match(TokenType::BlobLiteral)) {
        // TODO: Should the surrounding x' ' be removed here?
        auto value = consume().value();
        return create_ast_node<BlobLiteral>(value);
    }
    if (consume_if(TokenType::Null))
        return create_ast_node<NullLiteral>();

    return {};
}

Optional<NonnullRefPtr<Expression>> Parser::parse_column_name_expression()
{
    if (!match(TokenType::Identifier))
        return {};

    String first_identifier = consume(TokenType::Identifier).value();
    String schema_name;
    String table_name;
    String column_name;

    if (consume_if(TokenType::Period)) {
        String second_identifier = consume(TokenType::Identifier).value();

        if (consume_if(TokenType::Period)) {
            schema_name = move(first_identifier);
            table_name = move(second_identifier);
            column_name = consume(TokenType::Identifier).value();
        } else {
            table_name = move(first_identifier);
            column_name = move(second_identifier);
        }
    } else {
        column_name = move(first_identifier);
    }

    return create_ast_node<ColumnNameExpression>(move(schema_name), move(table_name), move(column_name));
}

Optional<NonnullRefPtr<Expression>> Parser::parse_unary_operator_expression()
{
    if (consume_if(TokenType::Minus))
        return create_ast_node<UnaryOperatorExpression>(UnaryOperator::Minus, parse_expression());

    if (consume_if(TokenType::Plus))
        return create_ast_node<UnaryOperatorExpression>(UnaryOperator::Plus, parse_expression());

    if (consume_if(TokenType::Tilde))
        return create_ast_node<UnaryOperatorExpression>(UnaryOperator::BitwiseNot, parse_expression());

    if (consume_if(TokenType::Not))
        return create_ast_node<UnaryOperatorExpression>(UnaryOperator::Not, parse_expression());

    return {};
}

Optional<NonnullRefPtr<Expression>> Parser::parse_binary_operator_expression(NonnullRefPtr<Expression> lhs)
{
    if (consume_if(TokenType::DoublePipe))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Concatenate, move(lhs), parse_expression());

    if (consume_if(TokenType::Asterisk))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Multiplication, move(lhs), parse_expression());

    if (consume_if(TokenType::Divide))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Division, move(lhs), parse_expression());

    if (consume_if(TokenType::Modulus))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Modulo, move(lhs), parse_expression());

    if (consume_if(TokenType::Plus))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Plus, move(lhs), parse_expression());

    if (consume_if(TokenType::Minus))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Minus, move(lhs), parse_expression());

    if (consume_if(TokenType::ShiftLeft))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::ShiftLeft, move(lhs), parse_expression());

    if (consume_if(TokenType::ShiftRight))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::ShiftRight, move(lhs), parse_expression());

    if (consume_if(TokenType::Ampersand))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::BitwiseAnd, move(lhs), parse_expression());

    if (consume_if(TokenType::Pipe))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::BitwiseOr, move(lhs), parse_expression());

    if (consume_if(TokenType::LessThan))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::LessThan, move(lhs), parse_expression());

    if (consume_if(TokenType::LessThanEquals))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::LessThanEquals, move(lhs), parse_expression());

    if (consume_if(TokenType::GreaterThan))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::GreaterThan, move(lhs), parse_expression());

    if (consume_if(TokenType::GreaterThanEquals))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::GreaterThanEquals, move(lhs), parse_expression());

    if (consume_if(TokenType::Equals) || consume_if(TokenType::EqualsEquals))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Equals, move(lhs), parse_expression());

    if (consume_if(TokenType::NotEquals1) || consume_if(TokenType::NotEquals2))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::NotEquals, move(lhs), parse_expression());

    if (consume_if(TokenType::And))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::And, move(lhs), parse_expression());

    if (consume_if(TokenType::Or))
        return create_ast_node<BinaryOperatorExpression>(BinaryOperator::Or, move(lhs), parse_expression());

    return {};
}

Optional<NonnullRefPtr<Expression>> Parser::parse_chained_expression()
{
    if (!match(TokenType::ParenOpen))
        return {};

    NonnullRefPtrVector<Expression> expressions;
    consume(TokenType::ParenOpen);

    do {
        expressions.append(parse_expression());
        if (match(TokenType::ParenClose))
            break;

        consume(TokenType::Comma);
    } while (!match(TokenType::Eof));

    consume(TokenType::ParenClose);

    return create_ast_node<ChainedExpression>(move(expressions));
}

Optional<NonnullRefPtr<Expression>> Parser::parse_cast_expression()
{
    if (!match(TokenType::Cast))
        return {};

    consume(TokenType::Cast);
    consume(TokenType::ParenOpen);
    auto expression = parse_expression();
    consume(TokenType::As);
    auto type_name = parse_type_name();
    consume(TokenType::ParenClose);

    return create_ast_node<CastExpression>(move(expression), move(type_name));
}

Optional<NonnullRefPtr<Expression>> Parser::parse_case_expression()
{
    if (!match(TokenType::Case))
        return {};

    consume();

    RefPtr<Expression> case_expression;
    if (!match(TokenType::When)) {
        case_expression = parse_expression();
    }

    Vector<CaseExpression::WhenThenClause> when_then_clauses;

    do {
        consume(TokenType::When);
        auto when = parse_expression();
        consume(TokenType::Then);
        auto then = parse_expression();

        when_then_clauses.append({ move(when), move(then) });

        if (!match(TokenType::When))
            break;
    } while (!match(TokenType::Eof));

    RefPtr<Expression> else_expression;
    if (consume_if(TokenType::Else))
        else_expression = parse_expression();

    consume(TokenType::End);
    return create_ast_node<CaseExpression>(move(case_expression), move(when_then_clauses), move(else_expression));
}

Optional<NonnullRefPtr<Expression>> Parser::parse_collate_expression(NonnullRefPtr<Expression> expression)
{
    if (!match(TokenType::Collate))
        return {};

    consume();
    String collation_name = consume(TokenType::Identifier).value();

    return create_ast_node<CollateExpression>(move(expression), move(collation_name));
}

Optional<NonnullRefPtr<Expression>> Parser::parse_is_expression(NonnullRefPtr<Expression> expression)
{
    if (!match(TokenType::Is))
        return {};

    consume();

    bool invert_expression = false;
    if (match(TokenType::Not)) {
        consume();
        invert_expression = true;
    }

    auto rhs = parse_expression();
    return create_ast_node<IsExpression>(move(expression), move(rhs), invert_expression);
}

Optional<NonnullRefPtr<Expression>> Parser::parse_match_expression(NonnullRefPtr<Expression> lhs, bool invert_expression)
{
    auto parse_escape = [this]() {
        RefPtr<Expression> escape;
        if (consume_if(TokenType::Escape))
            escape = parse_expression();
        return escape;
    };

    if (consume_if(TokenType::Like))
        return create_ast_node<MatchExpression>(MatchOperator::Like, move(lhs), parse_expression(), parse_escape(), invert_expression);

    if (consume_if(TokenType::Glob))
        return create_ast_node<MatchExpression>(MatchOperator::Glob, move(lhs), parse_expression(), parse_escape(), invert_expression);

    if (consume_if(TokenType::Match))
        return create_ast_node<MatchExpression>(MatchOperator::Match, move(lhs), parse_expression(), parse_escape(), invert_expression);

    if (consume_if(TokenType::Regexp))
        return create_ast_node<MatchExpression>(MatchOperator::Regexp, move(lhs), parse_expression(), parse_escape(), invert_expression);

    return {};
}

Optional<NonnullRefPtr<Expression>> Parser::parse_null_expression(NonnullRefPtr<Expression> expression, bool invert_expression)
{
    if (!match(TokenType::Isnull) && !match(TokenType::Notnull) && !(invert_expression && match(TokenType::Null)))
        return {};

    auto type = consume().type();
    invert_expression |= (type == TokenType::Notnull);

    return create_ast_node<NullExpression>(move(expression), invert_expression);
}

Optional<NonnullRefPtr<Expression>> Parser::parse_between_expression(NonnullRefPtr<Expression> expression, bool invert_expression)
{
    if (!match(TokenType::Between))
        return {};

    consume();

    auto nested = parse_expression();
    if (!is<BinaryOperatorExpression>(*nested)) {
        expected("Binary Expression");
        return create_ast_node<ErrorExpression>();
    }

    const auto& binary_expression = static_cast<const BinaryOperatorExpression&>(*nested);
    if (binary_expression.type() != BinaryOperator::And) {
        expected("AND Expression");
        return create_ast_node<ErrorExpression>();
    }

    return create_ast_node<BetweenExpression>(move(expression), binary_expression.lhs(), binary_expression.rhs(), invert_expression);
}

Optional<NonnullRefPtr<Expression>> Parser::parse_in_expression(NonnullRefPtr<Expression> expression, bool invert_expression)
{
    if (!match(TokenType::In))
        return {};

    consume();

    if (consume_if(TokenType::ParenOpen)) {
        if (match(TokenType::Select)) {
            // FIXME: Parse "select-stmt".
            return {};
        }

        // FIXME: Consolidate this with parse_chained_expression(). That method consumes the opening paren as
        //        well, and also requires at least one expression (whereas this allows for an empty chain).
        NonnullRefPtrVector<Expression> expressions;

        if (!match(TokenType::ParenClose)) {
            do {
                expressions.append(parse_expression());
                if (match(TokenType::ParenClose))
                    break;

                consume(TokenType::Comma);
            } while (!match(TokenType::Eof));
        }

        consume(TokenType::ParenClose);

        auto chain = create_ast_node<ChainedExpression>(move(expressions));
        return create_ast_node<InChainedExpression>(move(expression), move(chain), invert_expression);
    }

    String schema_or_table_name = consume(TokenType::Identifier).value();
    String schema_name;
    String table_name;

    if (consume_if(TokenType::Period)) {
        schema_name = move(schema_or_table_name);
        table_name = consume(TokenType::Identifier).value();
    } else {
        table_name = move(schema_or_table_name);
    }

    if (match(TokenType::ParenOpen)) {
        // FIXME: Parse "table-function".
        return {};
    }

    return create_ast_node<InTableExpression>(move(expression), move(schema_name), move(table_name), invert_expression);
}

NonnullRefPtr<ColumnDefinition> Parser::parse_column_definition()
{
    // https://sqlite.org/syntax/column-def.html
    auto name = consume(TokenType::Identifier).value();

    auto type_name = match(TokenType::Identifier)
        ? parse_type_name()
        // https://www.sqlite.org/datatype3.html: If no type is specified then the column has affinity BLOB.
        : create_ast_node<TypeName>("BLOB", NonnullRefPtrVector<SignedNumber> {});

    // FIXME: Parse "column-constraint".

    return create_ast_node<ColumnDefinition>(move(name), move(type_name));
}

NonnullRefPtr<TypeName> Parser::parse_type_name()
{
    // https: //sqlite.org/syntax/type-name.html
    auto name = consume(TokenType::Identifier).value();
    NonnullRefPtrVector<SignedNumber> signed_numbers;

    if (consume_if(TokenType::ParenOpen)) {
        signed_numbers.append(parse_signed_number());

        if (consume_if(TokenType::Comma))
            signed_numbers.append(parse_signed_number());

        consume(TokenType::ParenClose);
    }

    return create_ast_node<TypeName>(move(name), move(signed_numbers));
}

NonnullRefPtr<SignedNumber> Parser::parse_signed_number()
{
    // https://sqlite.org/syntax/signed-number.html
    bool is_positive = true;

    if (consume_if(TokenType::Plus))
        is_positive = true;
    else if (consume_if(TokenType::Minus))
        is_positive = false;

    if (match(TokenType::NumericLiteral)) {
        auto number = consume(TokenType::NumericLiteral).double_value();
        return create_ast_node<SignedNumber>(is_positive ? number : (number * -1));
    }

    expected("NumericLiteral");
    return create_ast_node<SignedNumber>(0);
}

Token Parser::consume()
{
    auto old_token = m_parser_state.m_token;
    m_parser_state.m_token = m_parser_state.m_lexer.next();
    return old_token;
}

Token Parser::consume(TokenType expected_type)
{
    if (!match(expected_type)) {
        expected(Token::name(expected_type));
    }
    return consume();
}

bool Parser::consume_if(TokenType expected_type)
{
    if (!match(expected_type))
        return false;

    consume();
    return true;
}

bool Parser::match(TokenType type) const
{
    return m_parser_state.m_token.type() == type;
}

void Parser::expected(StringView what)
{
    syntax_error(String::formatted("Unexpected token {}, expected {}", m_parser_state.m_token.name(), what));
}

void Parser::syntax_error(String message)
{
    m_parser_state.m_errors.append({ move(message), position() });
}

Parser::Position Parser::position() const
{
    return {
        m_parser_state.m_token.line_number(),
        m_parser_state.m_token.line_column()
    };
}

Parser::ParserState::ParserState(Lexer lexer)
    : m_lexer(move(lexer))
    , m_token(m_lexer.next())
{
}

}
