// SPDX-License-Identifier: MPL-2.0
#ifndef MILKDROP_PRESET_SHADER_COMPAT_H
#define MILKDROP_PRESET_SHADER_COMPAT_H

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PresetShaderCompat
{
    namespace Detail
    {
        struct Token
        {
            size_t begin;
            std::string_view text;
            bool identifier;
        };

        inline bool IsIdentifierStart(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        inline bool IsIdentifierPart(char c)
        {
            return IsIdentifierStart(c) || (c >= '0' && c <= '9');
        }

        inline bool IsLineEnd(char c) { return c == '\r' || c == '\n' || c == '\1'; }

        inline std::vector<Token> Tokenise(std::string_view source, bool keepDirectives = true)
        {
            std::vector<Token> tokens;
            for (size_t i = 0; i < source.size();)
            {
                const size_t start = i;
                const char c = source[i];
                if (c == ' ' || c == '\t' || IsLineEnd(c))
                    ++i;
                else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    i += 2;
                    while (i < source.size() && !IsLineEnd(source[i]))
                        ++i;
                }
                else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*')
                {
                    const auto end = source.find("*/", i + 2);
                    i = end == std::string_view::npos ? source.size() : end + 2;
                }
                else if (c == '#' && keepDirectives)
                {
                    ++i;
                    while (i < source.size() && !IsLineEnd(source[i]))
                        ++i;
                    tokens.push_back({start, source.substr(start, i - start), false});
                }
                else if (c == '\'' || c == '"')
                {
                    ++i;
                    while (i < source.size())
                    {
                        if (source[i] == '\\' && i + 1 < source.size())
                            i += 2;
                        else if (source[i++] == c)
                            break;
                    }
                    tokens.push_back({start, source.substr(start, i - start), false});
                }
                else
                {
                    const bool identifier = IsIdentifierStart(c);
                    ++i;
                    if (identifier)
                        while (i < source.size() && IsIdentifierPart(source[i]))
                            ++i;
                    else if (c == ':' && i < source.size() && source[i] == ':')
                        ++i;
                    tokens.push_back({start, source.substr(start, i - start), identifier});
                }
            }
            return tokens;
        }

        inline bool DirectiveMentions(const std::vector<Token>& tokens, std::string_view name)
        {
            for (const auto& token : tokens)
                if (token.text.front() == '#')
                    for (const auto& part : Tokenise(token.text, false))
                        if (part.identifier && part.text == name)
                            return true;
            return false;
        }

        inline bool IsNumericExpression(std::string_view expression, const std::vector<std::string_view>& constants)
        {
            bool operand = true;
            int parentheses = 0;
            for (size_t i = 0; i < expression.size();)
            {
                const char c = expression[i];
                if (c == ' ' || c == '\t')
                    ++i;
                else if (c == '/' && i + 1 < expression.size() && expression[i + 1] == '/')
                    break;
                else if (c == '/' && i + 1 < expression.size() && expression[i + 1] == '*')
                {
                    const auto end = expression.find("*/", i + 2);
                    if (end == std::string_view::npos)
                        return false;
                    i = end + 2;
                }
                else if (operand && (c == '+' || c == '-'))
                {
                    if (i + 1 < expression.size() && expression[i + 1] == c)
                        return false;
                    ++i;
                }
                else if (operand && c == '(')
                {
                    ++parentheses;
                    ++i;
                }
                else if (!operand && c == ')' && parentheses > 0)
                {
                    --parentheses;
                    ++i;
                }
                else if (!operand && (c == '+' || c == '-' || c == '*' || c == '/' || c == '%'))
                {
                    operand = true;
                    ++i;
                }
                else if (operand && IsIdentifierStart(c))
                {
                    const size_t start = i++;
                    while (i < expression.size() && IsIdentifierPart(expression[i]))
                        ++i;
                    if (std::find(constants.begin(), constants.end(), expression.substr(start, i - start)) == constants.end())
                        return false;
                    operand = false;
                }
                else if (operand && ((c >= '0' && c <= '9') || c == '.'))
                {
                    const auto digit = [](char value) { return value >= '0' && value <= '9'; };
                    if (c == '0' && i + 1 < expression.size() && (expression[i + 1] == 'x' || expression[i + 1] == 'X'))
                    {
                        i += 2;
                        const size_t start = i;
                        while (i < expression.size() && (digit(expression[i]) ||
                            (expression[i] >= 'a' && expression[i] <= 'f') || (expression[i] >= 'A' && expression[i] <= 'F')))
                            ++i;
                        if (i == start)
                            return false;
                    }
                    else
                    {
                        size_t digits = 0;
                        while (i < expression.size() && digit(expression[i])) { ++i; ++digits; }
                        if (i < expression.size() && expression[i] == '.')
                        {
                            ++i;
                            while (i < expression.size() && digit(expression[i])) { ++i; ++digits; }
                        }
                        if (!digits)
                            return false;
                        if (i < expression.size() && (expression[i] == 'e' || expression[i] == 'E'))
                        {
                            ++i;
                            if (i < expression.size() && (expression[i] == '+' || expression[i] == '-'))
                                ++i;
                            const size_t start = i;
                            while (i < expression.size() && digit(expression[i]))
                                ++i;
                            if (i == start)
                                return false;
                        }
                    }
                    if (i < expression.size() && (expression[i] == 'f' || expression[i] == 'F' ||
                        expression[i] == 'u' || expression[i] == 'U'))
                        ++i;
                    operand = false;
                }
                else
                    return false;
            }
            return !operand && parentheses == 0;
        }

        inline bool HasOnlyNumericDefines(const std::vector<Token>& tokens, bool allowResourceAliases = false)
        {
            std::vector<std::string_view> constants;
            for (const auto& token : tokens)
            {
                if (token.text.front() != '#')
                    continue;
                if (token.text.find('\\') != std::string_view::npos)
                    return false;
                const auto parts = Tokenise(token.text, false);
                if (parts.size() < 4 || parts[0].text != "#" || parts[1].text != "define" || !parts[2].identifier)
                    return false;
                const auto name = parts[2].text;
                if (name == "aspect" || name == "_c0" || name == "ret1" || name == "shader_body" ||
                    name == "x" || name == "y" || name == "z" || name == "w" ||
                    name == "r" || name == "g" || name == "b" || name == "a")
                    return false;
                for (const auto type : {"float", "double", "half", "int", "uint", "bool", "dword", "vector", "matrix"})
                {
                    const std::string_view prefix(type);
                    if (name.substr(0, prefix.size()) == prefix &&
                        std::all_of(name.begin() + prefix.size(), name.end(), [](char c) { return (c >= '0' && c <= '9') || c == 'x'; }))
                        return false;
                }
                const size_t value = parts[2].begin + name.size();
                const bool numeric = IsNumericExpression(token.text.substr(value), constants);
                // Simple aliases for existing resources/intrinsics cannot inject
                // declarations, scopes or operators. Do not treat them as numbers.
                const bool alias = allowResourceAliases && parts.size() == 4 && parts[3].identifier &&
                    (parts[3].text == "saturate" || parts[3].text.substr(0, 8) == "sampler_" ||
                     parts[3].text.substr(0, 8) == "texsize_");
                if ((parts[3].text == "(" && parts[3].begin == value) ||
                    std::find(constants.begin(), constants.end(), name) != constants.end() || (!numeric && !alias))
                    return false;
                if (numeric) constants.push_back(name);
            }
            return true;
        }

        inline std::string ReplaceTokens(std::string_view source, const std::vector<Token>& tokens,
            const std::vector<std::pair<size_t, std::string_view>>& replacements)
        {
            std::string result;
            size_t copied = 0;
            for (const auto& replacement : replacements)
            {
                const auto& token = tokens[replacement.first];
                result.append(source.substr(copied, token.begin - copied));
                result.append(replacement.second);
                copied = token.begin + token.text.size();
            }
            result.append(source.substr(copied));
            return result;
        }

        inline bool HasAspectAlias(const std::vector<Token>& tokens)
        {
            int definitions = 0;
            for (const auto& token : tokens)
            {
                if (token.text.front() != '#')
                    continue;
                const auto parts = Tokenise(token.text, false);
                if (parts.size() > 1 && (parts[1].text == "if" || parts[1].text == "ifdef" ||
                    parts[1].text == "ifndef" || parts[1].text == "elif" || parts[1].text == "else"))
                    return false;
                if (parts.size() > 2 && (parts[1].text == "define" || parts[1].text == "undef") &&
                    parts[2].text == "_c0")
                    return false;
                for (const auto& part : parts)
                    if (part.identifier && part.text == "aspect")
                    {
                        if (parts.size() != 4 || parts[0].text != "#" || parts[1].text != "define" ||
                            parts[2].text != "aspect" || parts[3].text != "_c0")
                            return false;
                        ++definitions;
                    }
            }
            return definitions == 1;
        }

        inline bool HasGlobal(const std::vector<Token>& tokens, size_t end, std::string_view type, std::string_view name)
        {
            int braces = 0, parentheses = 0;
            for (size_t i = 0; i < end; ++i)
            {
                const auto text = tokens[i].text;
                if (braces == 0 && parentheses == 0 && text == type)
                {
                    bool declarator = true, found = false;
                    int nested = 0;
                    for (size_t j = i + 1; j < end; ++j)
                    {
                        const auto part = tokens[j].text;
                        if (declarator)
                        {
                            if (!tokens[j].identifier || j + 1 == end || tokens[j + 1].text == "(")
                                break;
                            if (part == name && tokens[j + 1].text != "[")
                                found = true;
                            declarator = false;
                        }
                        else if (part == "(" || part == "[")
                            ++nested;
                        else if (part == ")" || part == "]")
                            --nested;
                        else if (nested == 0 && part == ",")
                            declarator = true;
                        else if (nested == 0 && part == ";")
                        {
                            if (found)
                                return true;
                            break;
                        }
                        else if (part == "{" || part == "}")
                            break;
                    }
                }
                if (text == "{") ++braces;
                else if (text == "}") --braces;
                else if (text == "(") ++parentheses;
                else if (text == ")") --parentheses;
            }
            return false;
        }
    }

    struct ShaderBodySpan
    {
        size_t marker = 0;
        size_t openBrace = 0;
        size_t closeBrace = 0;
    };

    inline bool LocateShaderBody(std::string_view source, ShaderBodySpan& result)
    {
        const auto tokens = Detail::Tokenise(source);
        ShaderBodySpan found;
        bool markerFound = false;
        bool bodyClosed = false;
        int braces = 0;
        int conditionals = 0;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& token = tokens[i];
            if (!token.text.empty() && token.text.front() == '#')
            {
                const auto directive = Detail::Tokenise(token.text, false);
                if (directive.size() >= 2)
                {
                    const auto name = directive[1].text;
                    if (name == "if" || name == "ifdef" || name == "ifndef")
                        ++conditionals;
                    else if (name == "endif")
                    {
                        if (conditionals == 0)
                            return false;
                        --conditionals;
                    }
                }
                // Do not guess where an expanded brace or marker belongs.
                // Numeric/function macros without structural tokens are safe.
                for (const auto& part : directive)
                    if (part.text == "{" || part.text == "}" || part.text == "shader_body" || part.text == "\\")
                        return false;
                continue;
            }
            if (token.identifier && token.text == "shader_body")
            {
                if (markerFound || braces != 0 || conditionals != 0 ||
                    i + 1 >= tokens.size() || tokens[i + 1].text != "{")
                    return false;
                markerFound = true;
                found.marker = token.begin;
                found.openBrace = tokens[i + 1].begin;
            }
            if (token.text == "{" || token.text == "}")
            {
                // Conditional preprocessing can change which brace is the
                // body's terminator. Leave such uncertain source unsupported.
                if (conditionals != 0)
                    return false;
                braces += token.text == "{" ? 1 : -1;
                if (braces < 0)
                    return false;
                if (markerFound && !bodyClosed && braces == 0)
                {
                    found.closeBrace = token.begin;
                    bodyClosed = true;
                }
            }
        }
        if (!markerFound || !bodyClosed || braces != 0 || conditionals != 0)
            return false;
        result = found;
        return true;
    }

    inline std::string SanitiseLegacyShaderAnnotations(std::string_view source)
    {
        ShaderBodySpan body;
        if (!LocateShaderBody(source, body))
            return std::string(source);

        std::string result(source);
        const auto whitespace = [](char c) { return c == ' ' || c == '\t' || Detail::IsLineEnd(c); };
        size_t first = 0;
        while (first < source.size() && whitespace(source[first]))
            ++first;
        if (source.substr(first, 9) == ".#define ")
            result[first] = ' '; // An export's stray prefix is not part of the directive.
        size_t footer = body.closeBrace + 1;
        while (footer < source.size() && whitespace(source[footer]))
            ++footer;
        // Some legacy exports append an un-commented author credit and END.
        // Accept this exact non-HLSL footer only; preserve trailing functions,
        // declarations, directives and arbitrary malformed source for diagnostics.
        if (source.substr(footer, 11) == "written by " || source.substr(footer, 6) == "martin")
        {
            size_t end = footer + (source.substr(footer, 11) == "written by " ? 11 : 0);
            const size_t author = end;
            while (end < source.size() && !Detail::IsLineEnd(source[end]))
            {
                const char c = source[end];
                if (!Detail::IsIdentifierPart(c) && c != ' ' && c != '\t' && c != '.' && c != '-')
                    break;
                ++end;
            }
            if (end > author && end < source.size() && Detail::IsLineEnd(source[end]))
            {
                while (end < source.size() && whitespace(source[end]))
                    ++end;
                if (source.substr(end, 3) == "END")
                {
                    end += 3;
                    while (end < source.size() && whitespace(source[end]))
                        ++end;
                    if (end == source.size())
                        result.resize(body.closeBrace + 1);
                }
                else if (source.substr(footer, 17) == "written by martin" && source.substr(end, 6) == "FROM: ")
                {
                    // Another observed export has a source credit and a fixed
                    // thank-you line instead of END. Accept no executable tail.
                    end += 6;
                    const size_t title = end;
                    while (end < source.size() && (Detail::IsIdentifierPart(source[end]) ||
                        source[end] == ' ' || source[end] == '.' || source[end] == '-')) ++end;
                    if (end > title && end < source.size() && Detail::IsLineEnd(source[end]))
                    {
                        while (end < source.size() && whitespace(source[end])) ++end;
                        constexpr std::string_view thanks = "Thanks Martin! :-)";
                        if (source.substr(end, thanks.size()) == thanks)
                        {
                            end += thanks.size();
                            while (end < source.size() && whitespace(source[end])) ++end;
                            if (end == source.size()) result.resize(body.closeBrace + 1);
                        }
                    }
                }
            }
        }

        const auto tokens = Detail::Tokenise(result);
        for (size_t i = 1; i + 1 < tokens.size(); ++i)
        {
            const auto& token = tokens[i];
            if (token.text != "." || token.begin <= body.openBrace || token.begin >= body.closeBrace)
                continue;
            if (tokens[i - 1].text == ")" && tokens[i + 1].text == ")")
            {
                // An empty result selector in an argument, e.g. GetPixel(uv).),
                // is invalid HLSL. Preserve the call, comments and line numbers;
                // valid swizzles and numeric literals do not match this pattern.
                result[token.begin] = ' ';
                continue;
            }
            if ((tokens[i - 1].text != ";" && tokens[i - 1].text != "}") || tokens[i + 1].text != "}")
                continue;
            // A lone dot before a closing brace cannot be a member access or
            // numeric literal. Do not alter dots on executable source lines.
            size_t begin = token.begin, end = token.begin + 1;
            while (begin > 0 && (result[begin - 1] == ' ' || result[begin - 1] == '\t'))
                --begin;
            while (end < result.size() && (result[end] == ' ' || result[end] == '\t'))
                ++end;
            if (begin > 0 && Detail::IsLineEnd(result[begin - 1]) &&
                end < result.size() && (Detail::IsLineEnd(result[end]) || result.compare(end, 2, "//") == 0))
                result[token.begin] = ' ';
        }
        return result;
    }

    inline std::string GuardRecoveryRotationInitialisers(std::string_view source)
    {
        const auto tokens = Detail::Tokenise(source);
        ShaderBodySpan body;
        if (!LocateShaderBody(source, body) || Detail::DirectiveMentions(tokens, "uv") ||
            Detail::DirectiveMentions(tokens, "uv_orig"))
            return std::string(source);
        auto isQ = [](std::string_view name) {
            if (name.size() < 2 || name.size() > 3 || name[0] != 'q' || name[1] < '1' || name[1] > '9')
                return false;
            unsigned number = 0;
            for (size_t i = 1; i < name.size(); ++i)
            {
                if (name[i] < '0' || name[i] > '9') return false;
                number = number * 10 + name[i] - '0';
            }
            return number <= 64;
        };
        std::string result;
        size_t copied = 0;
        for (size_t i = 0; i + 13 < tokens.size(); ++i)
        {
            // Only the observed q-controlled complex-rotation form is guarded.
            // Its authored coefficients remain unchanged outside recovery.
            if (tokens[i].begin <= body.openBrace || tokens[i].begin >= body.closeBrace ||
                tokens[i].text != "float2x2" || !tokens[i + 1].identifier ||
                tokens[i + 2].text != "=" || tokens[i + 3].text != "{" ||
                !isQ(tokens[i + 4].text) || tokens[i + 5].text != "," ||
                !isQ(tokens[i + 6].text) || tokens[i + 7].text != "," ||
                tokens[i + 8].text != "-" || tokens[i + 9].text != tokens[i + 6].text ||
                tokens[i + 10].text != "," || tokens[i + 11].text != tokens[i + 4].text ||
                tokens[i + 12].text != "}" || tokens[i + 13].text != ";")
                continue;
            result.append(source.substr(copied, tokens[i + 3].begin - copied));
            result += "__md2_recover_rotation(float2x2(";
            result += tokens[i + 4].text;
            result += ",";
            result += tokens[i + 6].text;
            result += ",-";
            result += tokens[i + 6].text;
            result += ",";
            result += tokens[i + 4].text;
            result += "),uv,uv_orig,__md2_rotation_decay)";
            copied = tokens[i + 12].begin + 1;
            i += 12;
        }
        if (!copied) return std::string(source);
        result.append(source.substr(copied));
        return result;
    }

    inline std::string RewriteLegacyShadowInitialisers(std::string_view source, std::string_view includeText)
    {
        // PRO's legacy D3DX9 frontend binds these initialisers before the new local.
        auto tokens = Detail::Tokenise(source);
        // Only unconditional numeric macros cannot inject declarations or change bindings.
        if (!Detail::HasOnlyNumericDefines(tokens, true))
            return std::string(source);
        const bool maxMacro = Detail::DirectiveMentions(tokens, "max");
        const bool dotMacro = Detail::DirectiveMentions(tokens, "dot");
        const bool hasCompoundTypes = std::any_of(tokens.begin(), tokens.end(), [](const auto& token) {
            return token.text == "struct" || token.text == "typedef";
        });
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const auto& token) {
            return token.text.front() == '#'; }), tokens.end());
        size_t body = tokens.size();
        for (size_t i = 0; i + 1 < tokens.size(); ++i)
            if (tokens[i].text == "shader_body" && tokens[i + 1].text == "{")
            {
                if (body != tokens.size())
                    return std::string(source);
                body = i + 1;
            }
        if (body == tokens.size())
            return std::string(source);

        const auto include = Detail::Tokenise(includeText);
        const bool aspectGlobal = Detail::HasAspectAlias(include) &&
            Detail::HasGlobal(include, include.size(), "float4", "_c0");
        const bool ret1Global = Detail::HasGlobal(tokens, body, "float3", "ret1") &&
            !Detail::DirectiveMentions(include, "ret1");
        std::vector<std::pair<size_t, std::string_view>> replacements;
        bool localAspect = false, localC0 = false, localRet1 = false;
        int braces = 1, parentheses = 0;
        for (size_t i = body + 1; i < tokens.size() && braces > 0; ++i)
        {
            const auto text = tokens[i].text;
            // Only the observed immediate shader-body patterns are translated. Complex
            // declarations and nested scopes retain the compiler's original diagnostics.
            const auto previousToken = tokens[i - 1].text;
            const bool statementStart = previousToken == "{" || previousToken == ";" || previousToken == "}";
            if (braces == 1 && parentheses == 0 && statementStart)
            {
                const auto matches = [&](std::initializer_list<std::string_view> pattern) {
                    if (tokens.size() - i < pattern.size())
                        return false;
                    size_t offset = i;
                    for (const auto part : pattern)
                        if (tokens[offset++].text != part)
                            return false;
                    return true;
                };
                if (aspectGlobal && !localAspect && !localC0 &&
                    matches({"float", "aspect", "=", "aspect", ".", "x", "/", "aspect", ".", "y", ";"}))
                {
                    replacements.emplace_back(i + 3, "::_c0");
                    replacements.emplace_back(i + 7, "::_c0");
                }
                else if (aspectGlobal && !localAspect && !localC0 &&
                    matches({"float2", "aspect", "=", "float2", "(", "aspect", ".", "x", ",", "1", ".", "0", ")", ";"}))
                    replacements.emplace_back(i + 5, "::_c0");
                else if (ret1Global && !localRet1 && matches({"float3", "ret1", "=", "ret1", ";"}))
                    replacements.emplace_back(i + 3, "::ret1");
                else if (i + 4 < tokens.size() &&
                    (text == "float" || text == "float2" || text == "float3" || text == "float4") &&
                    (tokens[i + 1].text == "max" || tokens[i + 1].text == "dot") &&
                    tokens[i + 2].text == "=" && tokens[i + 3].text == tokens[i + 1].text &&
                    tokens[i + 4].text == "(")
                {
                    const auto name = tokens[i + 1].text;
                    const auto hasEarlierBinding = [&](const std::vector<Detail::Token>& list, size_t end) {
                        for (size_t j = 0; j < end; ++j)
                            if (list[j].text == name &&
                                (j == 0 || (list[j - 1].text != "." && list[j - 1].text != "::")) &&
                                (j + 1 == end || list[j + 1].text != "("))
                                return true;
                        return false;
                    };
                    if (!hasEarlierBinding(tokens, i) && !hasEarlierBinding(include, include.size()))
                    {
                        // Rename only the local binding and variable uses in
                        // its shader-body scope. Calls keep their intrinsic name.
                        const std::string_view renamed = name == "max" ? "__md2_legacy_max" : "__md2_legacy_dot";
                        const auto containsName = [&](const std::vector<Detail::Token>& list) {
                            return std::any_of(list.begin(), list.end(), [&](const auto& token) {return token.text == renamed;});
                        };
                        if (!containsName(tokens) && !containsName(include) &&
                            !(name == "max" ? maxMacro : dotMacro) && !hasCompoundTypes)
                        {
                            replacements.emplace_back(i + 1, renamed);
                            int scope = 1;
                            for (size_t j = i + 3; j < tokens.size(); ++j)
                            {
                                if (tokens[j].text == "{") ++scope;
                                else if (tokens[j].text == "}" && --scope == 0) break;
                                if (tokens[j].text == name && j + 1 < tokens.size() && tokens[j + 1].text != "(" &&
                                    tokens[j - 1].text != "." && tokens[j - 1].text != "::")
                                    replacements.emplace_back(j, renamed);
                            }
                        }
                    }
                }
            }

            if (braces == 1 && tokens[i].identifier && i > body + 1 && i + 1 < tokens.size())
            {
                const auto previous = tokens[i - 1].text;
                const auto next = tokens[i + 1].text;
                const bool declarator = (tokens[i - 1].identifier && previous != "return") ||
                    (previous == "," && parentheses == 0);
                if (declarator && (next == "=" || next == ";" || next == "," || next == "["))
                {
                    if (text == "aspect") localAspect = true;
                    else if (text == "_c0") localC0 = true;
                    else if (text == "ret1") localRet1 = true;
                }
            }
            if (text == "{") ++braces;
            else if (text == "}") --braces;
            else if (text == "(") ++parentheses;
            else if (text == ")") --parentheses;
        }

        std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) {return a.first < b.first;});
        replacements.erase(std::unique(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) {return a.first == b.first;}), replacements.end());
        return Detail::ReplaceTokens(source, tokens, replacements);
    }

    inline std::string RewriteLegacyDoubleTypes(std::string_view source, std::string_view includeText, std::string_view profile)
    {
        // D3DX9's legacy frontend implements double scalar/vector types as float.
        // Keep real shader-model-5 double arithmetic available to modern presets.
        if (profile.substr(0, 5) != "ps_4_")
            return std::string(source);
        const auto tokens = Detail::Tokenise(source);
        const auto include = Detail::Tokenise(includeText);
        if (!Detail::HasOnlyNumericDefines(tokens, true))
            return std::string(source);
        for (const auto& token : tokens)
            if (token.text == "typedef")
                return std::string(source);
        for (const auto& token : include)
            if (token.text == "typedef")
                return std::string(source);
        for (const auto name : {"double", "double1", "double2", "double3", "double4"})
            if (Detail::DirectiveMentions(include, name))
                return std::string(source);

        std::vector<std::pair<size_t, std::string_view>> replacements;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (!tokens[i].identifier || (i > 0 && (tokens[i - 1].text == "." || tokens[i - 1].text == "::")))
                continue;
            const auto type = tokens[i].text;
            if (type == "double") replacements.emplace_back(i, "float");
            else if (type == "double1") replacements.emplace_back(i, "float1");
            else if (type == "double2") replacements.emplace_back(i, "float2");
            else if (type == "double3") replacements.emplace_back(i, "float3");
            else if (type == "double4") replacements.emplace_back(i, "float4");
        }
        return Detail::ReplaceTokens(source, tokens, replacements);
    }

    inline std::string RewriteLegacyBooleanBindings(std::string_view source, std::string_view includeText)
    {
        auto tokens = Detail::Tokenise(source);
        const auto include = Detail::Tokenise(includeText);
        if (!Detail::HasOnlyNumericDefines(tokens, true) ||
            std::any_of(tokens.begin(), tokens.end(), [](const auto& t) {return t.text == "struct" || t.text == "typedef";}))
            return std::string(source);
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const auto& t) {return t.text.front() == '#';}), tokens.end());
        struct Edit {size_t begin, length; std::string text;};
        std::vector<Edit> edits;
        int braces = 0;
        for (size_t i = 0; i + 2 < tokens.size(); ++i)
        {
            if (tokens[i].text == "{") ++braces;
            else if (tokens[i].text == "}") --braces;
            if (tokens[i].text != "bool" || !tokens[i + 1].identifier ||
                (tokens[i + 2].text != ";" && tokens[i + 2].text != "=")) continue;
            const auto name = tokens[i + 1].text;
            if (Detail::DirectiveMentions(Detail::Tokenise(source), name) ||
                std::any_of(include.begin(), include.end(), [&](const auto& t) {return t.text == name;}) ||
                Detail::DirectiveMentions(include, name)) continue;
            // Only unique, unqualified bindings are safe. Retain all diagnostics
            // for shadowed names, parameters, arrays and aggregate declarations.
            bool unique = true;
            for (size_t j = 0; j < tokens.size(); ++j)
                if (j != i + 1 && tokens[j].text == name &&
                    ((j && tokens[j - 1].identifier && tokens[j - 1].text != "return") ||
                     (j && (tokens[j - 1].text == "." || tokens[j - 1].text == "::")))) unique = false;
            if (!unique) continue;
            bool written = false;
            for (size_t j = i + 3; j + 2 < tokens.size(); ++j)
            {
                if (tokens[j].text != name) continue;
                if (tokens[j + 1].text == "=" && tokens[j + 2].text != "=") written = true;
                const auto op = tokens[j + 1].text;
                if ((op != "*" && op != "/" && op != "+" && op != "-") || tokens[j + 2].text != "=") continue;
                size_t end = j + 3;
                int depth = 0;
                for (; end < tokens.size(); ++end)
                {
                    if (tokens[end].text == "(") ++depth;
                    else if (tokens[end].text == ")" && --depth < 0) break;
                    else if (tokens[end].text == ";" && depth == 0) break;
                    else if (tokens[end].text == "{" || tokens[end].text == "}") break;
                }
                if (end == tokens.size() || tokens[end].text != ";" || depth != 0) continue;
                const auto start = tokens[j].begin, rhs = tokens[j + 3].begin;
                edits.push_back({start, tokens[end].begin - start,
                    std::string(name) + "=(" + std::string(name) + std::string(op) + "(" +
                    std::string(source.substr(rhs, tokens[end].begin - rhs)) + "))"});
                written = true;
                j = end;
            }
            if (written && braces == 0 && tokens[i + 2].text == ";" &&
                (i == 0 || tokens[i - 1].text == ";" || tokens[i - 1].text == "}"))
                edits.push_back({tokens[i].begin, tokens[i].text.size(), "static bool"});
        }
        std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) {return a.begin > b.begin;});
        std::string result(source);
        for (const auto& edit : edits) result.replace(edit.begin, edit.length, edit.text);
        return result;
    }

    inline std::string RewriteLegacyReservedBinding(std::string_view source, std::string_view includeText, std::string_view profile,
        std::string_view name, std::string_view type, std::string_view renamed)
    {
        if (profile.substr(0, 5) != "ps_4_" && profile.substr(0, 5) != "ps_5_")
            return std::string(source);
        const auto tokens = Detail::Tokenise(source);
        const auto include = Detail::Tokenise(includeText);
        for (const auto& token : tokens)
            if (token.text.front() == '#' || token.text == "typedef" || token.text == renamed)
                return std::string(source);
        for (const auto& token : include)
            if (token.text == name || token.text == renamed || token.text == "typedef")
                return std::string(source);
            else if (token.text.front() == '#' && token.text.find('#', 1) != std::string_view::npos)
                return std::string(source);
        if (Detail::DirectiveMentions(include, name) || Detail::DirectiveMentions(include, renamed))
            return std::string(source);

        const size_t none = tokens.size();
        std::vector<size_t> blocks(tokens.size(), none), ends(tokens.size(), none), stack;
        size_t body = none;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            blocks[i] = stack.empty() ? none : stack.back();
            if (tokens[i].text == "{")
            {
                if (i > 0 && tokens[i - 1].text == "shader_body")
                {
                    if (body != none || !stack.empty())
                        return std::string(source);
                    body = i;
                }
                stack.push_back(i);
            }
            else if (tokens[i].text == "}")
            {
                if (stack.empty())
                    return std::string(source);
                ends[stack.back()] = i;
                stack.pop_back();
            }
        }
        if (body == none || !stack.empty())
            return std::string(source);

        // Accept one observed statement, rather than infer arbitrary HLSL declarations.
        size_t declaration = none;
        for (size_t i = body + 1; i + 2 < ends[body]; ++i)
            if (tokens[i].text == type && tokens[i + 1].text == name && tokens[i + 2].text == "=" &&
                (tokens[i - 1].text == "{" || tokens[i - 1].text == ";" || tokens[i - 1].text == "}"))
            {
                if (declaration != none)
                    return std::string(source);
                declaration = i + 1;
            }
        if (declaration == none || blocks[declaration] == none)
            return std::string(source);
        const size_t end = ends[blocks[declaration]];
        size_t initialiserEnd = none;
        int parentheses = 0, brackets = 0;
        for (size_t i = declaration + 2; i < end; ++i)
        {
            const auto text = tokens[i].text;
            if (text == "{" || text == "}" || (text == "," && parentheses == 0 && brackets == 0))
                return std::string(source);
            if (text == "(") ++parentheses;
            else if (text == ")") --parentheses;
            else if (text == "[") ++brackets;
            else if (text == "]") --brackets;
            else if (text == ";" && parentheses == 0 && brackets == 0)
            {
                initialiserEnd = i;
                break;
            }
            if (parentheses < 0 || brackets < 0)
                return std::string(source);
        }
        if (initialiserEnd == none)
            return std::string(source);

        std::vector<std::pair<size_t, std::string_view>> replacements;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (tokens[i].text != name || (i > 0 && tokens[i - 1].text == "."))
                continue;
            if (i != declaration)
            {
                // Every other unqualified name must be a use of this binding. Decline
                // globals, parameters, self-initialisation and another local declaration.
                if (i <= initialiserEnd || i >= end || i + 1 == tokens.size() ||
                    (tokens[i - 1].identifier && tokens[i - 1].text != "return") ||
                    tokens[i - 1].text == "::" || tokens[i + 1].text == "(" ||
                    (tokens[i - 1].text == "," && (tokens[i + 1].text == "=" || tokens[i + 1].text == ";" ||
                        tokens[i + 1].text == "," || tokens[i + 1].text == "[")))
                    return std::string(source);
            }
            replacements.emplace_back(i, renamed);
        }
        return Detail::ReplaceTokens(source, tokens, replacements);
    }

    inline std::string RewriteLegacyPointBinding(std::string_view source, std::string_view includeText, std::string_view profile)
    {
        if (profile.substr(0, 5) != "ps_4_")
            return std::string(source);
        return RewriteLegacyReservedBinding(source, includeText, profile, "point", "float2", "__md_legacy_point");
    }

    inline std::string RewriteLegacyLineBinding(std::string_view source, std::string_view includeText, std::string_view profile)
    {
        return RewriteLegacyReservedBinding(source, includeText, profile, "line", "float", "__md_legacy_line");
    }
}

#endif // MILKDROP_PRESET_SHADER_COMPAT_H
