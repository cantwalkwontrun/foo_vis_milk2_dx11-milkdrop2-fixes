// SPDX-License-Identifier: MPL-2.0
#ifndef MILKDROP_PRESET_EEL_COMPAT_H
#define MILKDROP_PRESET_EEL_COMPAT_H

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <charconv>
#include <unordered_set>
#include "eelrepaircatalog.h"

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
        inline bool Number(std::string_view token)
        {
            if (token.empty() || ((token.front() < '0' || token.front() > '9') && token.front() != '.')) return false;
            const char* end = token.data() + token.size();
            if (std::string_view("fFlL").find(token.back()) != std::string_view::npos) --end;
            double value = 0; const auto parsed = std::from_chars(token.data(), end, value);
            return parsed.ec == std::errc{} && parsed.ptr == end;
        }
        inline bool UnsafeRecoveryToken(const Token& token)
        {
            return (token.identifier && Equal(token.text, "function")) || token.text.front() == '"' || token.text.front() == '\'';
        }
        inline bool NumberedCodeKey(std::string_view name)
        {
            for (auto prefix : {std::string_view("per_frame_init_"), std::string_view("per_frame_"), std::string_view("per_pixel_")})
                if (name.size() > prefix.size() && Equal(name.substr(0,prefix.size()),prefix) &&
                    std::all_of(name.begin()+prefix.size(),name.end(),[](char c) { return c >= '0' && c <= '9'; })) return true;
            return false;
        }
    }

    inline std::string CompactLegacyLine(std::string_view source)
    {
        std::string result;
        for (char c:source)
            if (c!=' ' && c!='\t' && c!='\r' && c!='\n' && c!='\1')
                result+=c>='A' && c<='Z'?static_cast<char>(c+'a'-'A'):c;
        return result;
    }

    // Restore only exact, independently corroborated damaged statements. A
    // different prefix or an ordinary continued expression stays untouched.
    inline std::string RecoverKnownLegacyStatements(std::string_view source)
    {
        bool valid=false;const auto tokens=Syntax::Tokens(source,valid);
        if (!valid || std::any_of(tokens.begin(),tokens.end(),Syntax::UnsafeRecoveryToken)) return std::string(source);
        std::string result;size_t copied=0;
        for (size_t begin=0;begin<source.size();)
        {
            size_t end=source.find_first_of("\1\r\n",begin);if (end==std::string_view::npos) end=source.size();
            const auto line=source.substr(begin,end-begin);
            if (line.find("//")==std::string_view::npos && line.find("\\\\")==std::string_view::npos && line.find("/*")==std::string_view::npos)
            {
                const auto compact=CompactLegacyLine(line);
                auto next=std::lower_bound(tokens.begin(),tokens.end(),end,[](const auto& token,size_t position){return token.begin<position;});
                const bool boundary=next==tokens.end() || (next->identifier && next+1!=tokens.end() && (next+1)->text=="=");
                const Catalogue::Entry* match=nullptr;
                if (boundary) for (const auto& entry:Catalogue::Truncated) if (compact==entry.broken) { match=&entry;break; }
                if (boundary && !match) for (const auto& entry:Catalogue::RandomWrapper) if (compact==entry.broken) { match=&entry;break; }
                if (match)
                {
                    result.append(source.substr(copied,begin-copied));result+=match->complete;copied=end;
                }
                else if (end<source.size())
                {
                    size_t after=end+1;if (source[end]=='\r' && after<source.size() && source[after]=='\n') ++after;
                    size_t finish=source.find_first_of("\1\r\n",after);if (finish==std::string_view::npos) finish=source.size();
                    const auto suffix=source.substr(after,finish-after);
                    const auto merged=compact+CompactLegacyLine(suffix);
                    // Match the entire known zoom statement, including its
                    // coefficients and frequencies, rather than merging arbitrary numbers.
                    for (auto reference:Catalogue::SplitDecimalZoom)
                        if (compact.size()>=3 && compact.substr(compact.size()-3)=="0.5" &&
                            suffix.find("//")==std::string_view::npos && suffix.find("/*")==std::string_view::npos && merged==reference)
                        {
                            result.append(source.substr(copied,begin-copied));result+=reference;copied=finish;end=finish;break;
                        }
                }
            }
            begin=end<source.size()?end+1:end;
        }
        result.append(source.substr(copied));return result;
    }

    // Partial recovery only: a final empty assignment has no RHS to execute.
    // Keep every complete preceding statement, leave the original source intact,
    // and require the caller to report that the expression is still incomplete.
    inline std::string RecoverIncompleteTrailingAssignment(std::string_view source)
    {
        bool valid=false;const auto tokens=Syntax::Tokens(source,valid);
        if (!valid || tokens.size()<2 || std::any_of(tokens.begin(),tokens.end(),Syntax::UnsafeRecoveryToken)) return std::string(source);
        const size_t tail=tokens.size()-2;
        if (!tokens[tail].identifier || tokens.back().text!="=" || (tail && tokens[tail-1].text!=";")) return std::string(source);
        std::vector<char> groups;
        for (size_t i=0;i<tail;++i)
        {
            const auto text=tokens[i].text;
            if (text=="(" || text=="[") groups.push_back(text[0]);
            else if (text==")" || text=="]")
            {
                if (groups.empty() || groups.back()!=(text==")"?'(':'[')) return std::string(source);
                groups.pop_back();
            }
        }
        if (!groups.empty()) return std::string(source);
        return tail?std::string(source.substr(0,tokens[tail].begin)):std::string("0;");
    }

    // Candidates for explicitly warned partial recovery only. Split complete
    // top-level statements, never inner function/loop bodies. Extra closing
    // parentheses stay in their damaged statement for the compiler to reject;
    // unclosed groups, mixed brackets, declarations and quotes are ambiguous.
    // Bound both input and compiler retries to keep damaged imports responsive.
    inline std::vector<std::string_view> PartialRecoveryStatements(std::string_view source)
    {
        if (source.size()>65536) return {};
        bool valid=false;const auto tokens=Syntax::Tokens(source,valid);
        if (!valid || std::any_of(tokens.begin(),tokens.end(),Syntax::UnsafeRecoveryToken)) return {};
        std::vector<std::string_view> statements;std::vector<char> groups;
        size_t begin=0;bool active=false;
        for (const auto& token:tokens)
        {
            if (!active && token.text==";") continue;
            if (!active) { begin=token.begin;active=true; }
            if (token.text=="(" || token.text=="[")
            {
                if (groups.size()>=256) return {};
                groups.push_back(token.text[0]);
            }
            else if ((token.text==")" || token.text=="]") && !groups.empty())
            {
                if (groups.back()!=(token.text==")"?'(':'[')) return {};
                groups.pop_back();
            }
            if (token.text==";" && groups.empty())
            {
                statements.push_back(source.substr(begin,token.begin+1-begin));active=false;
                if (statements.size()>32) return {};
            }
        }
        if (!groups.empty()) return {};
        if (active) statements.push_back(source.substr(begin));
        if (statements.size()<2 || statements.size()>32) return {};
        return statements;
    }

    // Last-resort approximations, never an assertion of the author's intent.
    // The caller must try normal/corroborated compilation first and explicitly
    // report any successful inference. Keep unknown functions and ambiguous
    // opening groups intact rather than manufacturing arbitrary expressions.
    template<class IsFunction>
    inline std::string ApproximateDamagedMaths(std::string_view source, IsFunction isFunction)
    {
        if (source.size()>65536) return std::string(source);
        bool valid=false;auto tokens=Syntax::Tokens(source,valid);
        if (!valid || std::any_of(tokens.begin(),tokens.end(),Syntax::UnsafeRecoveryToken)) return std::string(source);
        std::string working(source);
        // With no RHS tokens at all, hold the existing value (initially zero in
        // EEL) instead of inventing motion or a new coefficient. Preserve comments.
        if (RecoverIncompleteTrailingAssignment(source)!=source)
            working.insert(tokens.back().begin+1," "+std::string(tokens[tokens.size()-2].text)+";");
        // Observed premature conditional closing and missing comparison bound.
        // Zero is a neutral comparison reference, not a recovered author value.
        constexpr std::string_view broken="my=if(above(x*.3)),.2,my);";
        constexpr std::string_view inferred="my=if(above(x*.3,0),.2,my);";
        if (CompactLegacyLine(source)==broken) working=inferred;
        else
        {
            const auto statements=PartialRecoveryStatements(source);
            for (auto it=statements.rbegin();it!=statements.rend();++it)
                if (CompactLegacyLine(*it)==broken)
                    working.replace(size_t(it->data()-source.data()),it->size(),inferred);
        }
        tokens=Syntax::Tokens(working,valid);
        if (!valid) return std::string(source);
        struct Edit { size_t position; size_t length; std::string replacement; };
        std::vector<Edit> edits;std::vector<char> groups;
        std::unordered_set<std::string> assigned,pending;
        for (size_t i=0;i<tokens.size();++i)
        {
            const auto& token=tokens[i];
            if (token.identifier && i+1<tokens.size() && tokens[i+1].text=="=")
                pending.insert(CompactLegacyLine(token.text));
            if (token.identifier && i+1<tokens.size() && tokens[i+1].text=="(" &&
                assigned.contains(CompactLegacyLine(token.text)) && !isFunction(std::string(token.text).c_str()))
                edits.push_back({tokens[i+1].begin,0,"*"});
            if (token.text=="(" || token.text=="[")
            {
                if (groups.size()>=256) return std::string(source);
                groups.push_back(token.text[0]);
            }
            else if (token.text==")" || token.text=="]")
            {
                if (groups.empty())
                {
                    if (token.text!=")") return std::string(source);
                    edits.push_back({token.begin,1,""});
                }
                else
                {
                    if (groups.back()!=(token.text==")"?'(':'[')) return std::string(source);
                    groups.pop_back();
                }
            }
            if (token.text==";" && groups.empty())
            {
                assigned.insert(pending.begin(),pending.end());pending.clear();
            }
            if (token.text=="." && i && i+2<tokens.size() && (tokens[i-1].text=="+" || tokens[i-1].text=="-") &&
                tokens[i+1].text=="*" && tokens[i+1].begin==token.begin+1 && tokens[i+2].begin==token.begin+2)
            {
                const auto value=tokens[i+2].text;
                if (!value.empty() && std::all_of(value.begin(),value.end(),[](char c){return c>='0' && c<='9';}))
                    edits.push_back({tokens[i+1].begin,1,""}); // . * 3 -> .3, retaining the supplied digit.
                else if (i+3<tokens.size() && tokens[i+3].text=="(" &&
                         (Syntax::Equal(value,"sin") || Syntax::Equal(value,"cos")) && isFunction(std::string(value).c_str()))
                    edits.push_back({token.begin,1,"0"}); // Missing additive coefficient contributes zero.
            }
            if (edits.size()>64) return std::string(source);
        }
        if (!groups.empty()) return std::string(source);
        std::sort(edits.begin(),edits.end(),[](const auto& a,const auto& b){return a.position>b.position;});
        for (const auto& edit:edits) working.replace(edit.position,edit.length,edit.replacement);
        return working;
    }

    // Retry-only repairs for observed import/editing artefacts. Commented
    // assignments may have an orphaned binary continuation on the next line;
    // require a complete preceding statement and one balanced suffix ending ';'.
    inline std::string RecoverImportAndCommentArtefacts(std::string_view source)
    {
        bool valid = false; const auto tokens = Syntax::Tokens(source, valid);
        if (!valid || std::any_of(tokens.begin(),tokens.end(),Syntax::UnsafeRecoveryToken)) return std::string(source);
        std::string result(source);
        size_t cursor = 0; int depth = 0;
        for (size_t begin = 0; begin < source.size();)
        {
            size_t end = source.find_first_of("\1\r\n",begin); if (end == std::string_view::npos) end = source.size();
            while (cursor < tokens.size() && tokens[cursor].begin < begin)
            {
                const auto text=tokens[cursor++].text;
                if (text=="(" || text=="[") ++depth;
                if (text==")" || text=="]") --depth;
            }
            const auto line=source.substr(begin,end-begin);
            bool lineValid=false;const auto words=Syntax::Tokens(line,lineValid);
            // A second serialised code key with a comment-only RHS is an import
            // artefact, not a completed assignment to be given a fabricated value.
            if (!depth && (!cursor || tokens[cursor-1].text==";") && !tokens.empty() && tokens.back().begin<end &&
                lineValid && words.size()==2 && words[0].identifier && Syntax::NumberedCodeKey(words[0].text) &&
                words[1].text=="=" && (line.find("//")!=std::string_view::npos || line.find("\\\\")!=std::string_view::npos))
                std::fill(result.begin()+begin,result.begin()+end,' ');
            const auto first=line.find_first_not_of(" \t");
            if (!depth && (!cursor || tokens[cursor-1].text==";") && first!=std::string_view::npos &&
                (line.substr(first,2)=="//" || line.substr(first,2)=="\\\\") && end<source.size())
            {
                bool bodyValid=false;const auto body=Syntax::Tokens(line.substr(first+2),bodyValid);
                int balance=0;bool assignment=bodyValid && body.size()>2 && body[0].identifier && body[1].text=="=";
                for (const auto& word:body)
                {
                    if (word.text==";" || Syntax::UnsafeRecoveryToken(word)) assignment=false;
                    if (word.text=="(" || word.text=="[") ++balance;
                    if ((word.text==")" || word.text=="]") && --balance<0) assignment=false;
                }
                size_t next=end+1;if (source[end]=='\r' && next<source.size() && source[next]=='\n') ++next;
                size_t finish=source.find_first_of("\1\r\n",next);if (finish==std::string_view::npos) finish=source.size();
                bool suffixValid=false;const auto suffix=Syntax::Tokens(source.substr(next,finish-next),suffixValid);
                bool continuation=assignment && !balance && suffixValid && suffix.size()>2 &&
                    suffix[0].text.size()==1 && std::string_view("*/%^").find(suffix[0].text[0])!=std::string_view::npos && suffix.back().text==";";
                balance=0;
                for (size_t i=0;i<suffix.size();++i)
                {
                    const auto& word=suffix[i];
                    if (Syntax::Assignment(word.text) || Syntax::UnsafeRecoveryToken(word) || (word.text==";" && i+1!=suffix.size())) continuation=false;
                    if (word.text=="(" || word.text=="[") ++balance;
                    if ((word.text==")" || word.text=="]") && --balance<0) continuation=false;
                }
                if (continuation && !balance) std::fill(result.begin()+next,result.begin()+finish,' ');
            }
            begin=end<source.size()?end+1:end;
        }
        return result;
    }

    // Retain supplied arithmetic: accept numeric coefficient brackets, remove
    // doubled separators before a same-line binary continuation, and ignore an
    // isolated unmatched ');' after a complete top-level statement. Unknown calls,
    // missing operands and unfinished opening groups remain errors.
    inline std::string RecoverArithmeticPunctuation(std::string_view source)
    {
        bool valid=false;const auto tokens=Syntax::Tokens(source,valid);
        if (!valid || std::any_of(tokens.begin(),tokens.end(),Syntax::UnsafeRecoveryToken)) return std::string(source);
        struct Edit { size_t position; bool insert; };
        std::vector<Edit> edits;std::vector<char> groups;
        for (size_t i=0;i<tokens.size();++i)
        {
            const auto& token=tokens[i];
            if (!token.identifier && Syntax::Number(token.text) && i+1<tokens.size() && tokens[i+1].text=="(" &&
                token.begin+token.text.size()==tokens[i+1].begin)
                edits.push_back({tokens[i+1].begin,true});
            if (token.text==";" && i && (tokens[i-1].identifier || Syntax::Number(tokens[i-1].text) || tokens[i-1].text==")" || tokens[i-1].text=="]"))
            {
                size_t next=i;while (next<tokens.size() && tokens[next].text==";") ++next;
                if (next-i>=2 && next<tokens.size() && tokens[next].text.size()==1 &&
                    std::string_view("*/%^").find(tokens[next].text[0])!=std::string_view::npos)
                {
                    const size_t start=tokens[i-1].begin+tokens[i-1].text.size();
                    const auto gap=source.substr(start,tokens[next].begin-start);
                    if (std::all_of(gap.begin(),gap.end(),[](char c) { return c==' ' || c=='\t' || c==';'; }))
                        for (size_t j=i;j<next;++j) edits.push_back({tokens[j].begin,false});
                }
                i=next-1;continue;
            }
            if (token.text=="(" || token.text=="[") groups.push_back(token.text[0]);
            else if (token.text==")" || token.text=="]")
            {
                if (token.text==")" && groups.empty() && i && tokens[i-1].text==";" && i+1<tokens.size() && tokens[i+1].text==";")
                { edits.push_back({token.begin,false});continue; }
                if (groups.empty() || groups.back()!=(token.text==")"?'(':'[')) return std::string(source);
                groups.pop_back();
            }
        }
        if (!groups.empty() || edits.empty()) return std::string(source);
        std::sort(edits.begin(),edits.end(),[](auto a,auto b) { return a.position>b.position; });
        std::string result(source);
        for (const auto& edit:edits)
            if (edit.insert) result.insert(edit.position,1,'*');else result.erase(edit.position,1);
        return result;
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

    // The projectM scanner reserves every intrinsic name, including bare names
    // used as variables. Keep calls and variables separate, and lower extended
    // identifier bytes to deterministic ASCII names without altering stored text.
    template<typename IsFunction>
    inline std::string RewriteLegacyVariables(std::string_view source, IsFunction isFunction)
    {
        bool valid = false; const auto tokens = Syntax::Tokens(source, valid);
        if (!valid) return std::string(source);
        for (const auto& token : tokens)
            if (token.identifier && Syntax::Equal(token.text, "function")) return std::string(source);
        std::string result; size_t copied = 0;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& token = tokens[i];
            if (!token.identifier) continue;
            const bool extended = std::any_of(token.text.begin(), token.text.end(), [](unsigned char c) { return c >= 128; });
            const bool dotted = token.text.find('.') != std::string_view::npos && token.text.front()!='.' &&
                token.text.back()!='.' && token.text.find("..")==std::string_view::npos;
            const bool call = i + 1 < tokens.size() && tokens[i + 1].text == "(";
            if (!extended && !dotted && (call || !isFunction(std::string(token.text).c_str()))) continue;
            // Unknown calls remain errors rather than being invented as functions.
            if (call) continue;
            result.append(source.substr(copied, token.begin - copied));
            result += "__md2_legacy_var_";
            constexpr char hex[] = "0123456789abcdef";
            for (unsigned char c : token.text)
            {
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                result += hex[c >> 4]; result += hex[c & 15];
            }
            copied = token.begin + token.text.size();
        }
        result.append(source.substr(copied)); return result;
    }

    // Failed-compilation fallback only: remove empty top-level statements, a
    // standalone dot before a signed numeric operand, and recognised C floating
    // suffixes after the exact corroborated statement repairs above. Generic
    // punctuation repairs never supply missing operands or closing parentheses.
    inline std::string RecoverLegacyExpressionSyntax(std::string_view source)
    {
        const auto known = RecoverKnownLegacyStatements(source);
        const auto artefacts = RecoverImportAndCommentArtefacts(known);
        const auto punctuation = RecoverArithmeticPunctuation(artefacts);
        const auto separated = RecoverMissingStatementSeparators(punctuation);
        bool valid = false; const auto tokens = Syntax::Tokens(separated, valid);
        if (!valid) return separated;
        std::vector<size_t> removals;
        int depth = 0; bool statementStart = true, operand = true;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& token = tokens[i];
            if ((token.identifier && Syntax::Equal(token.text, "function")) || token.text.front() == '"' || token.text.front() == '\'')
                return separated;
            if (!depth && token.text == ";" && statementStart) { removals.push_back(token.begin); continue; }
            if (token.text == "." && operand && i + 2 < tokens.size() &&
                (tokens[i + 1].text == "+" || tokens[i + 1].text == "-") &&
                !tokens[i + 2].identifier && ((tokens[i + 2].text.front() >= '0' && tokens[i + 2].text.front() <= '9') ||
                                            tokens[i + 2].text.front() == '.'))
            { removals.push_back(token.begin); continue; }
            if (!token.identifier && token.text.size() > 1 && std::string_view("fFlL").find(token.text.back()) != std::string_view::npos)
            {
                double value = 0; const char* end = token.text.data() + token.text.size() - 1;
                const auto parsed = std::from_chars(token.text.data(), end, value);
                if (parsed.ec == std::errc{} && parsed.ptr == end) removals.push_back(token.begin + token.text.size() - 1);
            }
            if (token.text == "(" || token.text == "[") ++depth;
            if (token.text == ")" || token.text == "]") --depth;
            if (depth < 0) return separated;
            statementStart = !depth && token.text == ";";
            operand = !(token.identifier || (token.text.front() >= '0' && token.text.front() <= '9') ||
                        token.text.front() == '.' || token.text == ")" || token.text == "]");
        }
        if (depth || removals.empty()) return separated;
        std::string result = separated;
        for (auto it = removals.rbegin(); it != removals.rend(); ++it) result.erase(*it, 1);
        return result;
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
