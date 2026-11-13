// SPDX-License-Identifier: MPL-2.0
#ifndef MILKDROP_PRESET_EEL_COMPAT_H
#define MILKDROP_PRESET_EEL_COMPAT_H

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace PresetEelCompat
{
    inline constexpr unsigned WhileIterationBudget = 1048576;
    inline constexpr const char* FrameWhileLimit = "__md2_frame_while_limit";
    inline constexpr const char* FrameWhileGuardActive = "__md2_frame_while_active";
    inline constexpr const char* SoundFunction = "__md2_sound_query";

    namespace Syntax
    {
        struct Token { std::string_view text; size_t begin; bool identifier; bool lineBefore; };
        inline bool NameStart(unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 128; }
        inline bool NamePart(unsigned char c)
        { return NameStart(c) || (c >= '0' && c <= '9') || c == '.'; }
        inline bool Equal(std::string_view first, std::string_view second)
        {
            if (first.size() != second.size()) return false;
            for (size_t i = 0; i < first.size(); ++i)
            {
                char c = first[i]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c != second[i]) return false;
            }
            return true;
        }
        inline std::vector<Token> Tokens(std::string_view source, bool& valid)
        {
            std::vector<Token> result;
            valid = true; bool line = false;
            for (size_t i = 0; i < source.size();)
            {
                const char c = source[i];
                if (c == '\1' || c == '\r' || c == '\n') { line = true; ++i; continue; }
                if (c == ' ' || c == '\t') { ++i; continue; }
                if (i + 1 < source.size() && ((c == '/' && source[i + 1] == '/') || (c == '\\' && source[i + 1] == '\\')))
                {
                    i += 2;
                    while (i < source.size() && source[i] != '\1' && source[i] != '\r' && source[i] != '\n') ++i;
                    continue;
                }
                if (c == '/' && i + 1 < source.size() && source[i + 1] == '*')
                {
                    const auto end = source.find("*/", i + 2);
                    if (end == std::string_view::npos) { valid = false; return {}; }
                    if (source.substr(i, end - i).find_first_of("\1\r\n") != std::string_view::npos) line = true;
                    i = end + 2; continue;
                }
                const size_t begin = i++;
                const bool identifier = NameStart(static_cast<unsigned char>(c));
                if (c == '\'' || c == '"')
                {
                    bool closed = false;
                    while (i < source.size())
                        if (source[i] == '\\' && i + 1 < source.size()) i += 2;
                        else if (source[i++] == c) { closed = true; break; }
                    if (!closed) { valid = false; return {}; }
                }
                else if (identifier || (c >= '0' && c <= '9') || c == '.')
                    while (i < source.size() && NamePart(static_cast<unsigned char>(source[i]))) ++i;
                else if (i < source.size() && source[i] == '=' &&
                    std::string_view("=<>!+-*/%&|^").find(c) != std::string_view::npos) ++i;
                result.push_back({source.substr(begin, i - begin), begin, identifier, line});
                line = false;
            }
            return result;
        }
        inline bool Assignment(std::string_view token)
        {
            return token == "=" || (token.size() == 2 && token[1] == '=' &&
                std::string_view("+-*/%&|^").find(token[0]) != std::string_view::npos);
        }
    }

    // Keep a variable named sound usable. Only actual calls are lowered to the
    // private host function; comments, strings and user function declarations stay intact.
    inline std::string RewriteSoundCalls(std::string_view source)
    {
        bool valid = false; const auto tokens = Syntax::Tokens(source, valid);
        if (!valid) return std::string(source);
        for (const auto& token : tokens)
            if (token.identifier && Syntax::Equal(token.text, "function")) return std::string(source);
        std::string result; size_t copied = 0;
        for (size_t i = 0; i + 1 < tokens.size(); ++i)
            if (tokens[i].identifier && Syntax::Equal(tokens[i].text, "sound") && tokens[i + 1].text == "(")
            {
                result.append(source.substr(copied, tokens[i].begin - copied));
                result += SoundFunction; copied = tokens[i].begin + tokens[i].text.size();
            }
        result.append(source.substr(copied)); return result;
    }

    // Used only after compilation fails. Insert a separator between a complete
    // top-level assignment and a new assignment on a later source line. Do not
    // guess inside parentheses, ternaries, declarations or unbalanced source.
    inline std::string RecoverMissingStatementSeparators(std::string_view source)
    {
        bool valid = false; const auto tokens = Syntax::Tokens(source, valid);
        if (!valid) return std::string(source);
        int parentheses = 0, brackets = 0, ternaries = 0;
        bool assigned = false, canEnd = false;
        std::vector<size_t> insertions;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& token = tokens[i];
            if ((token.identifier && Syntax::Equal(token.text, "function")) || token.text.front() == '"' || token.text.front() == '\'')
                return std::string(source);
            const bool top = parentheses == 0 && brackets == 0;
            if (top && !ternaries && assigned && canEnd && token.lineBefore && token.identifier &&
                i + 1 < tokens.size() && Syntax::Assignment(tokens[i + 1].text))
            {
                insertions.push_back(token.begin); assigned = false;
            }
            if (top && Syntax::Assignment(token.text)) assigned = true;
            if (top && token.text == ";") assigned = false;
            if (top && token.text == "?") ++ternaries;
            if (top && token.text == ":" && ternaries) --ternaries;
            if (token.text == "(") ++parentheses;
            if (token.text == ")" && --parentheses < 0) return std::string(source);
            if (token.text == "[") ++brackets;
            if (token.text == "]" && --brackets < 0) return std::string(source);
            canEnd = token.identifier || (token.text.front() >= '0' && token.text.front() <= '9') ||
                token.text.front() == '.' || token.text == ")" || token.text == "]";
        }
        if (parentheses || brackets || ternaries || insertions.empty()) return std::string(source);
        std::string result; size_t copied = 0;
        for (size_t position : insertions)
        {
            result.append(source.substr(copied, position - copied)); result += ';'; copied = position;
        }
        result.append(source.substr(copied)); return result;
    }

    // The EEL engine limits each loop separately. Nested non-terminating
    // whiles can multiply that allowance into trillions of iterations.
    // Share each outer while's allowance with its directly nested whiles.
    // Sequential loops retain independent allowances. Preserve the separate
    // grammar of EEL function declarations.
    inline std::string BoundWhileIterations(std::string_view source, unsigned budget = WhileIterationBudget,
        std::string_view dynamicLimit = {})
    {
        struct Edit { size_t position; bool opening; unsigned group; };
        struct Parenthesis { bool loop; int previousGroup; };
        std::vector<Edit> edits;
        std::vector<Parenthesis> parentheses;
        unsigned groups = 0;
        int activeGroup = -1;
        bool pendingWhile = false;
        const auto namePart = [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$' || c >= 128;
        };
        for (size_t i = 0; i < source.size();)
        {
            const char c = source[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\1') { ++i; continue; }
            if (i + 1 < source.size() && ((c == '/' && source[i + 1] == '/') || (c == '\\' && source[i + 1] == '\\')))
            {
                i += 2;
                while (i < source.size() && source[i] != '\r' && source[i] != '\n' && source[i] != '\1') ++i;
                continue;
            }
            if (c == '/' && i + 1 < source.size() && source[i + 1] == '*')
            {
                const auto end = source.find("*/", i + 2);
                if (end == std::string_view::npos) return std::string(source);
                i = end + 2; continue;
            }
            if (c == '\'' || c == '"')
            {
                pendingWhile = false; ++i;
                bool closed = false;
                while (i < source.size())
                    if (source[i] == '\\' && i + 1 < source.size()) i += 2;
                    else if (source[i++] == c) { closed = true; break; }
                if (!closed) return std::string(source);
                continue;
            }
            if (namePart(static_cast<unsigned char>(c)))
            {
                const size_t begin = i++;
                while (i < source.size() && namePart(static_cast<unsigned char>(source[i]))) ++i;
                std::string name(source.substr(begin, i - begin));
                for (char& ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                if (name == "function") return std::string(source);
                pendingWhile = name == "while";
                continue;
            }
            if (c == '(')
            {
                parentheses.push_back({pendingWhile, activeGroup});
                if (pendingWhile)
                {
                    if (activeGroup < 0) activeGroup = static_cast<int>(groups++);
                    edits.push_back({i + 1, true, static_cast<unsigned>(activeGroup)});
                }
            }
            else if (c == ')')
            {
                if (parentheses.empty()) return std::string(source);
                if (parentheses.back().loop) edits.push_back({i, false, static_cast<unsigned>(activeGroup)});
                activeGroup = parentheses.back().previousGroup;
                parentheses.pop_back();
            }
            pendingWhile = false; ++i;
        }
        if (!parentheses.empty() || edits.empty()) return std::string(source);
        uint64_t identity = 14695981039346656037ULL;
        std::string lower(source);
        for (unsigned char c : source) { identity ^= c; identity *= 1099511628211ULL; }
        for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        std::string counter = "__md2_while_guard_" + std::to_string(identity);
        while (lower.find(counter) != std::string::npos) counter += '_';
        std::stable_sort(edits.begin(), edits.end(), [](auto a, auto b) { return a.position < b.position; });
        std::string result;
        std::vector<std::string> guards;
        const std::string limit = dynamicLimit.empty() ? std::to_string(budget) :
            "min(" + std::to_string(budget) + "," + std::string(dynamicLimit) + ")";
        for (unsigned group = 0; group < groups; ++group)
        {
            const std::string name = counter + "_" + std::to_string(group);
            result += name + "=0;";
            guards.push_back("(" + name + "=" + name + "+1)<=" + limit + "&&(");
        }
        size_t copied = 0;
        for (const auto& edit : edits)
        {
            result.append(source.substr(copied, edit.position - copied));
            result += edit.opening ? guards[edit.group] : ")";
            copied = edit.position;
        }
        result.append(source.substr(copied));
        return result;
    }

    // Reserved implementation names: keep ordinary preset variables such as mouse_x distinct.
    inline constexpr const char* MouseNames[4] = {
        "__md3_mouse_x", "__md3_mouse_y", "__md3_mouse_z", "__md3_mouse_w"};

    inline std::string RewriteMouseIdentifiers(std::string_view source)
    {
        std::string result;
        result.reserve(source.size());

        const auto isNamePart = [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$' || c >= 0x80;
        };
        const auto lowerAscii = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
        };

        for (size_t i = 0; i < source.size();)
        {
            const size_t start = i;
            const char c = source[i];
            if (i + 1 < source.size() &&
                ((c == '/' && source[i + 1] == '/') || (c == '\\' && source[i + 1] == '\\')))
            {
                i += 2;
                while (i < source.size() && source[i] != '\r' && source[i] != '\n' && source[i] != '\1')
                    ++i;
            }
            else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*')
            {
                const auto end = source.find("*/", i + 2);
                i = end == std::string_view::npos ? source.size() : end + 2;
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
            }
            else if (isNamePart(static_cast<unsigned char>(c)))
            {
                do
                    ++i;
                while (i < source.size() && isNamePart(static_cast<unsigned char>(source[i])));

                const auto name = source.substr(start, i - start);
                if (name.size() == 7 && lowerAscii(name[0]) == 'm' && lowerAscii(name[1]) == 'o' &&
                    lowerAscii(name[2]) == 'u' && lowerAscii(name[3]) == 's' &&
                    lowerAscii(name[4]) == 'e' && name[5] == '.')
                {
                    const char component = lowerAscii(name[6]);
                    const int index = component == 'x' ? 0 : component == 'y' ? 1 :
                        component == 'z' ? 2 : component == 'w' ? 3 : -1;
                    if (index >= 0)
                    {
                        result += MouseNames[index];
                        continue;
                    }
                }
            }
            else
                ++i;

            result.append(source.data() + start, i - start);
        }
        return result;
    }
}

#endif // MILKDROP_PRESET_EEL_COMPAT_H
