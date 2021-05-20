#include "src/parser/include/antlr_parser/parser_error_listener.h"

#include "src/common/include/utils.h"

using namespace graphflow::common;

namespace graphflow {
namespace parser {

void ParserErrorListener::syntaxError(Recognizer* recognizer, Token* offendingSymbol, size_t line,
    size_t charPositionInLine, const std::string& msg, std::exception_ptr e) {
    auto finalError = msg + " (line: " + to_string(line) +
                      ", offset: " + to_string(charPositionInLine) + ")\n" +
                      formatUnderLineError(*recognizer, *offendingSymbol, line, charPositionInLine);
    throw invalid_argument(finalError);
}

string ParserErrorListener::formatUnderLineError(
    Recognizer& recognizer, const Token& offendingToken, size_t line, size_t charPositionInLine) {
    auto tokens = (CommonTokenStream*)recognizer.getInputStream();
    auto input = tokens->getTokenSource()->getInputStream()->toString();
    auto errorLine = StringUtils::split(input, "\n")[line - 1];
    auto underLine = string(" ");
    for (auto i = 0u; i < charPositionInLine; ++i) {
        underLine += " ";
    }
    for (auto i = offendingToken.getStartIndex(); i <= offendingToken.getStopIndex(); ++i) {
        underLine += "^";
    }
    return "\"" + errorLine + "\"\n" + underLine;
}

} // namespace parser
} // namespace graphflow
