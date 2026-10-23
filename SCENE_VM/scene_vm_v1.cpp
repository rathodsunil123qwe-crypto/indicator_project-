#include "scene_vm_v1_interface.h"
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IndicatorSceneVM {
namespace {

// ============================================================================
// SCENE VM V1 IMPLEMENTATION CONTRACT
// ============================================================================
// This file implements only the language/runtime boundary. It deliberately
// contains NO PID, encoder, homing, stall recovery, WS2812 transport, GPIO,
// controller mapping, or LittleFS code. Those domains remain behind MotionPort
// and LEDPort. This separation is intentional and must not be collapsed.
//
// The public LEDSelector interface is frozen and carries only a resolved index.
// Dynamic selector identity therefore lives internally in Cmd::selectorName;
// the VM resolves it immediately before calling LEDPort. Static selector lists
// are expanded into individual adapter calls because the frozen adapter API
// represents one resolved index at a time.
//
// Execution is compiled into a flat instruction stream. A single persistent PC
// plus a persistent FOR-frame stack makes WAIT/WAIT TARGET fully resumable even
// when the wait occurs inside IF/ELSE, WHILE, or FOR. No recursive runBlock()
// with a local child index is used, because that pattern loses continuation.
// ============================================================================

static std::string trim(std::string s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
static bool ieq(const std::string& a, const std::string& b) { return lower(a) == lower(b); }
// Scene comments are deliberately simple: everything after '#' is ignored.
// This matches the Builder's V9.1 authoring behavior and keeps comments out of
// command parsing without adding a second comment grammar to the language.
static std::string stripComment(std::string s) {
    const size_t p = s.find('#');
    if (p != std::string::npos) s.erase(p);
    return trim(s);
}

static bool identifier(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!(std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) return false;
    return true;
}
static bool splitTop(const std::string& s, char sep, std::vector<std::string>& out) {
    int par = 0, br = 0; size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') ++par; else if (s[i] == ')') --par;
        else if (s[i] == '[') ++br; else if (s[i] == ']') --br;
        else if (s[i] == sep && par == 0 && br == 0) { out.push_back(trim(s.substr(start, i-start))); start = i+1; }
    }
    out.push_back(trim(s.substr(start)));
    return par == 0 && br == 0;
}

struct Value {
    enum Type { NUMBER, COLOR, BOOL } type = NUMBER;
    double number = 0.0;
    SceneColor color{0,0,0};
    bool boolean = false;
    static Value num(double v){ Value x; x.type=NUMBER; x.number=v; return x; }
    static Value col(SceneColor v){ Value x; x.type=COLOR; x.color=v; return x; }
    static Value booleanV(bool v){ Value x; x.type=BOOL; x.boolean=v; return x; }
};
using Env = std::unordered_map<std::string, Value>;

struct Token { std::string s; };
static std::vector<Token> lex(const std::string& s, std::string& err) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        if (std::isspace(ch)) { ++i; continue; }
        if (std::isdigit(ch) || s[i] == '.') {
            const size_t j0 = i; bool dot = false; bool digit = false;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
                if (s[i] == '.') { if (dot) { err="invalid number"; return {}; } dot=true; }
                else digit=true;
                ++i;
            }
            if (!digit) { err="invalid number"; return {}; }
            out.push_back({s.substr(j0, i-j0)}); continue;
        }
        if (std::isalpha(ch) || s[i] == '_') {
            const size_t j0=i++; while (i<s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i]=='_')) ++i;
            out.push_back({s.substr(j0,i-j0)}); continue;
        }
        if (i+1<s.size()) {
            const std::string two=s.substr(i,2);
            if (two=="=="||two=="!="||two==">="||two=="<=") { out.push_back({two}); i+=2; continue; }
        }
        if (std::string("+-*/%(),<>;").find(s[i]) != std::string::npos) { out.push_back({std::string(1,s[i])}); ++i; continue; }
        err="invalid expression token"; return {};
    }
    return out;
}

struct Expr { virtual ~Expr()=default; virtual bool eval(const Env&,uint64_t,uint64_t,Value&,std::string&) const=0; };
using ExprPtr=std::unique_ptr<Expr>;
struct Lit:Expr { Value v; explicit Lit(Value x):v(x){} bool eval(const Env&,uint64_t,uint64_t,Value&o,std::string&)const override{o=v;return true;} };
struct Var:Expr {
    std::string n; explicit Var(std::string x):n(std::move(x)){}
    bool eval(const Env& e,uint64_t t,uint64_t d,Value&o,std::string&err)const override {
        const std::string k=lower(n);
        if(k=="time"){o=Value::num(static_cast<double>(t));return true;}
        if(k=="delta"){o=Value::num(static_cast<double>(d));return true;}
        const auto it=e.find(k); if(it==e.end()){err="undefined variable: "+n;return false;} o=it->second; return true;
    }
};
struct Unary:Expr {
    char op; ExprPtr a; Unary(char c,ExprPtr x):op(c),a(std::move(x)){}
    bool eval(const Env&e,uint64_t t,uint64_t d,Value&o,std::string&err)const override{Value x;if(!a->eval(e,t,d,x,err))return false;if(x.type!=Value::NUMBER){err="unary operator requires NUMBER";return false;}o=Value::num(op=='-'?-x.number:x.number);return true;}
};
struct Binary:Expr {
    std::string op; ExprPtr a,b; Binary(std::string o,ExprPtr x,ExprPtr y):op(std::move(o)),a(std::move(x)),b(std::move(y)){}
    bool eval(const Env&e,uint64_t t,uint64_t d,Value&o,std::string&err)const override {
        Value x,y; if(!a->eval(e,t,d,x,err)||!b->eval(e,t,d,y,err))return false;
        if(op=="=="||op=="!="){
            if(x.type!=y.type){err="comparison operands must have compatible types";return false;}
            bool z=false; if(x.type==Value::NUMBER)z=x.number==y.number; else if(x.type==Value::BOOL)z=x.boolean==y.boolean; else z=x.color.r==y.color.r&&x.color.g==y.color.g&&x.color.b==y.color.b;
            o=Value::booleanV(op=="=="?z:!z); return true;
        }
        if(x.type!=Value::NUMBER||y.type!=Value::NUMBER){err="numeric operator requires NUMBER";return false;}
        if(op==">") {o=Value::booleanV(x.number>y.number);return true;} if(op=="<"){o=Value::booleanV(x.number<y.number);return true;} if(op==">="){o=Value::booleanV(x.number>=y.number);return true;} if(op=="<="){o=Value::booleanV(x.number<=y.number);return true;}
        double z=0; if(op=="+")z=x.number+y.number; else if(op=="-")z=x.number-y.number; else if(op=="*")z=x.number*y.number; else if(op=="/"){if(y.number==0){err="division by zero";return false;}z=x.number/y.number;} else if(op=="%")z=std::fmod(x.number,y.number); else {err="unknown operator";return false;} o=Value::num(z); return true;
    }
};
struct Call:Expr {
    std::string fn; std::vector<ExprPtr>a; Call(std::string f,std::vector<ExprPtr>x):fn(std::move(f)),a(std::move(x)){}
    bool eval(const Env&e,uint64_t t,uint64_t d,Value&o,std::string&err)const override {
        std::string u=upper(fn); std::vector<Value>v; v.reserve(a.size()); for(const auto&x:a){Value q;if(!x->eval(e,t,d,q,err))return false;v.push_back(q);}
        const auto nums=[&](){for(const auto&q:v)if(q.type!=Value::NUMBER)return false;return true;};
        if(u=="RANDOM"){if(!v.empty()){err="RANDOM takes 0 arguments";return false;}o=Value::num(static_cast<double>(std::rand())/static_cast<double>(RAND_MAX));return true;}
        if(u=="RGB"){if(v.size()!=3||!nums()){err="RGB requires 3 NUMBER arguments";return false;}for(const auto&q:v)if(q.number<0||q.number>255){err="RGB values must be 0..255";return false;}o=Value::col(SceneColor{static_cast<uint8_t>(std::lround(v[0].number)),static_cast<uint8_t>(std::lround(v[1].number)),static_cast<uint8_t>(std::lround(v[2].number))});return true;}
        if(u=="RANDOM_CHOICE"){if(v.empty()){err="RANDOM_CHOICE requires at least one argument";return false;}for(size_t i=1;i<v.size();++i)if(v[i].type!=v[0].type){err="RANDOM_CHOICE choices must have one compatible type";return false;}const size_t idx=static_cast<size_t>(std::rand())%v.size();o=v[idx];return true;}
        if(!nums()){err=u+" requires numeric arguments";return false;}
        double z=0; bool ok=true;
        if(u=="ABS"&&v.size()==1)z=std::fabs(v[0].number); else if(u=="MIN"&&v.size()==2)z=std::min(v[0].number,v[1].number); else if(u=="MAX"&&v.size()==2)z=std::max(v[0].number,v[1].number);
        else if(u=="CLAMP"&&v.size()==3)z=std::clamp(v[0].number,v[1].number,v[2].number);
        else if(u=="MAP"&&v.size()==5){if(v[2].number==v[1].number){err="MAP input range cannot be zero";return false;}z=v[3].number+(v[0].number-v[1].number)*(v[4].number-v[3].number)/(v[2].number-v[1].number);}
        else if(u=="LERP"&&v.size()==3)z=v[0].number+(v[1].number-v[0].number)*v[2].number;
        else if(u=="SIN"&&v.size()==1)z=std::sin(v[0].number); else if(u=="COS"&&v.size()==1)z=std::cos(v[0].number); else if(u=="TAN"&&v.size()==1)z=std::tan(v[0].number); else ok=false;
        if(!ok){err="invalid function or arity: "+fn;return false;} o=Value::num(z);return true;
    }
};

class EParser {
    std::vector<Token> t; size_t i=0; std::string& err;
    const Token* peek()const{return i<t.size()?&t[i]:nullptr;}
    bool take(const char*s){if(peek()&&ieq(peek()->s,s)){++i;return true;}return false;}
    ExprPtr cmp(){auto x=add();if(!x)return{};while(peek()&&(peek()->s=="=="||peek()->s=="!="||peek()->s==">"||peek()->s=="<"||peek()->s==">="||peek()->s=="<=")){std::string o=peek()->s;++i;auto y=add();if(!y)return{};x=std::make_unique<Binary>(o,std::move(x),std::move(y));}return x;}
    ExprPtr add(){auto x=mul();if(!x)return{};while(peek()&&(peek()->s=="+"||peek()->s=="-")){std::string o=peek()->s;++i;auto y=mul();if(!y)return{};x=std::make_unique<Binary>(o,std::move(x),std::move(y));}return x;}
    ExprPtr mul(){auto x=un();if(!x)return{};while(peek()&&(peek()->s=="*"||peek()->s=="/"||peek()->s=="%")){std::string o=peek()->s;++i;auto y=un();if(!y)return{};x=std::make_unique<Binary>(o,std::move(x),std::move(y));}return x;}
    ExprPtr un(){if(peek()&&(peek()->s=="+"||peek()->s=="-")){const char c=peek()->s[0];++i;auto x=un();if(!x)return{};return std::make_unique<Unary>(c,std::move(x));}return prim();}
    ExprPtr prim(){
        if(!peek()){err="missing expression operand";return{};} const std::string s=peek()->s;
        if(take("(")){auto x=cmp();if(!take(")")){err="missing ')'";return{};}return x;}
        ++i; if(std::isdigit(static_cast<unsigned char>(s[0]))||s[0]=='.')return std::make_unique<Lit>(Value::num(std::stod(s)));
        if(!identifier(s)){err="invalid expression identifier: "+s;return{};}
        if(ieq(s,"TRUE")) return std::make_unique<Lit>(Value::booleanV(true));
        if(ieq(s,"FALSE")) return std::make_unique<Lit>(Value::booleanV(false));
        if(peek()&&peek()->s=="("){++i;std::vector<ExprPtr>a;if(!take(")")){while(true){auto x=cmp();if(!x)return{};a.push_back(std::move(x));if(take(")"))break;if(!take(",")){err="expected ',' or ')'";return{};}}}return std::make_unique<Call>(s,std::move(a));}
        return std::make_unique<Var>(s);
    }
public:
    EParser(std::vector<Token>x,std::string&e):t(std::move(x)),err(e){}
    ExprPtr parse(){auto x=cmp();if(!x)return{};if(peek()){err="unexpected token: "+peek()->s;return{};}return x;}
};
static ExprPtr parseExpr(const std::string&s,std::string&err){auto t=lex(s,err);if(!err.empty())return{};return EParser(std::move(t),err).parse();}

struct SelectorSpec { bool has=false; bool dynamic=false; std::string variable; std::vector<int32_t> indices; };
struct Cmd {
    enum Kind { MOVE,STOP,WAIT,WAIT_TARGET,SET,CLEAR,SHOW,LET,IF,WHILE,FOR } kind;
    Side side=Side::NONE; Zone zone=Zone::FRONTLEDS; SelectorSpec selector;
    ExprPtr a,b,c; std::string var; std::vector<std::unique_ptr<Cmd>> body,els;
    bool hasBrightness=false; int brightness=0;
};

static bool parseSide(const std::string&s,Side&o){const std::string u=upper(trim(s));if(u=="LEFT")o=Side::LEFT;else if(u=="RIGHT")o=Side::RIGHT;else if(u=="BOTH")o=Side::BOTH;else return false;return true;}
static bool parseZone(const std::string&s,Zone&o){const std::string u=upper(trim(s));if(u=="FRONTLEDS")o=Zone::FRONTLEDS;else if(u=="MAINLEDS")o=Zone::MAINLEDS;else if(u=="LENS")o=Zone::LENS;else if(u=="BACKLEDS")o=Zone::BACKLEDS;else if(u=="NO_PLATELEDS")o=Zone::NO_PLATELEDS;else return false;return true;}

static bool parseUnsignedInt(const std::string&raw, uint64_t&out, uint64_t maxValue) {
    const std::string s=trim(raw);
    if(s.empty()) return false;
    uint64_t v=0;
    for(char ch:s){
        if(!std::isdigit(static_cast<unsigned char>(ch))) return false;
        const uint64_t d=static_cast<uint64_t>(ch-'0');
        if(v>(maxValue-d)/10ULL) return false;
        v=v*10ULL+d;
    }
    out=v;
    return true;
}

static size_t findKeywordTopLevel(const std::string&s,const std::string&keyword) {
    int par=0,br=0;
    const std::string k=lower(keyword);
    for(size_t i=0;i<s.size();++i){
        if(s[i]=='(') ++par;
        else if(s[i]==')') --par;
        else if(s[i]=='[') ++br;
        else if(s[i]==']') --br;
        if(par==0 && br==0 && i+k.size()<=s.size()){
            bool match=true;
            for(size_t j=0;j<k.size();++j){
                if(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i+j])))!=k[j]){match=false;break;}
            }
            if(match){
                const bool leftOk=(i==0)||std::isspace(static_cast<unsigned char>(s[i-1]));
                const bool rightOk=(i+k.size()==s.size())||std::isspace(static_cast<unsigned char>(s[i+k.size()]));
                if(leftOk&&rightOk) return i;
            }
        }
    }
    return std::string::npos;
}

static bool takeWord(const std::string&s,size_t&pos,std::string&word) {
    while(pos<s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    const size_t a=pos;
    while(pos<s.size() && !std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    if(a==pos) return false;
    word=s.substr(a,pos-a);
    return true;
}

static bool parseSelector(const std::string&raw,SelectorSpec&out,std::string&err){
    out=SelectorSpec{};
    const std::string s=trim(raw);
    if(s.size()<3||s.front()!='['||s.back()!=']'){err="invalid selector";return false;}
    const std::string inside=trim(s.substr(1,s.size()-2));
    if(identifier(inside)){out.has=true;out.dynamic=true;out.variable=lower(inside);return true;}
    if(inside.empty()){err="invalid selector";return false;}
    std::vector<std::string> parts;
    if(!splitTop(inside,',',parts)){err="invalid selector";return false;}
    for(const std::string&p0:parts){
        const std::string p=trim(p0);
        const size_t dash=p.find('-');
        if(dash==std::string::npos){
            uint64_t n=0;
            if(!parseUnsignedInt(p,n,2147483647ULL)){err="invalid selector range: "+p;return false;}
            out.indices.push_back(static_cast<int32_t>(n));
        } else {
            if(p.find('-',dash+1)!=std::string::npos){err="invalid selector range: "+p;return false;}
            uint64_t a=0,b=0;
            if(!parseUnsignedInt(trim(p.substr(0,dash)),a,2147483647ULL)||!parseUnsignedInt(trim(p.substr(dash+1)),b,2147483647ULL)){
                err="invalid selector range: "+p;return false;
            }
            if(a>b){err="selector range must be ascending: "+p;return false;}
            for(uint64_t x=a;x<=b;++x){out.indices.push_back(static_cast<int32_t>(x));if(x==b)break;}
        }
    }
    out.has=true;out.dynamic=false;return true;
}

class VMImpl final : public SceneVM {
    struct SceneMeta { std::string id,name,desc,category; std::vector<std::string> caps; std::unordered_map<std::string,SceneColor> colors; } meta;
    struct Instr { enum Op { MOVE,STOP,WAIT,WAIT_TARGET,SET,CLEAR,SHOW,LET,IF_FALSE,JUMP,WHILE_CHECK,FOR_INIT,FOR_CHECK,FOR_STEP,FOR_END } op; Cmd* cmd=nullptr; size_t jump=0; };
    struct ForFrame { std::string var; bool hadOld=false; Value old{}; };
    MotionPort* motion=nullptr; LEDPort* leds=nullptr; std::vector<std::unique_ptr<Cmd>> tree; std::vector<Instr> code; Env env; std::vector<ForFrame> forStack;
    SceneMeta loadedMeta; SceneContext context{}; VMState st=VMState::IDLE; VMResult loadResult=VMResult::INVALID; std::string err;
    size_t pc=0; uint32_t lastNow=0,waitUntil=0; uint64_t timeMs=0,deltaMs=0; Side waitSide=Side::BOTH; Side currentTargetSide=Side::NONE; bool clockStarted=false;
    Side lastMoveSide=Side::NONE; uint8_t lastMovePos=0; uint8_t lastMoveSpeed=100; bool lastMotionWasMove=false;

    bool fail(const std::string&e){err=e;st=VMState::ERROR;if(motion)motion->stop(Side::BOTH);return false;}
    static bool hasCap(const SceneMeta&m,const std::string&c){return std::find(m.caps.begin(),m.caps.end(),c)!=m.caps.end();}
    bool eval(const ExprPtr&e,Value&o){if(!e){return fail("missing expression");}std::string q;if(!e->eval(env,timeMs,deltaMs,o,q))return fail(q);return true;}
    bool assign(const std::string&name,const Value&v){const std::string k=lower(name);if(k=="time"||k=="delta"){return fail("TIME and DELTA are read-only");}if(loadedMeta.colors.find(k)!=loadedMeta.colors.end())return fail("LET cannot redefine editable color slot: "+name);env[k]=v;return true;}

    bool compileBlock(const std::vector<std::unique_ptr<Cmd>>&b){
        for(const auto&up:b){Cmd*c=up.get();
            if(c->kind==Cmd::IF){const size_t at=code.size();code.push_back({Instr::IF_FALSE,c,0});if(!compileBlock(c->body))return false;const size_t jumpEnd=code.size();code.push_back({Instr::JUMP,c,0});code[at].jump=code.size();if(!compileBlock(c->els))return false;code[jumpEnd].jump=code.size();}
            else if(c->kind==Cmd::WHILE){const size_t check=code.size();code.push_back({Instr::WHILE_CHECK,c,0});if(!compileBlock(c->body))return false;code.push_back({Instr::JUMP,c,check});code[check].jump=code.size();}
            else if(c->kind==Cmd::FOR){const size_t init=code.size();code.push_back({Instr::FOR_INIT,c,0});const size_t check=code.size();code.push_back({Instr::FOR_CHECK,c,0});if(!compileBlock(c->body))return false;code.push_back({Instr::FOR_STEP,c,0});code.push_back({Instr::JUMP,c,check});const size_t end=code.size();code.push_back({Instr::FOR_END,c,0});code[init].jump=end;code[check].jump=end;}
            else code.push_back({static_cast<Instr::Op>(c->kind),c,0});
        } return true;
    }

    bool validateRestrictions(const std::vector<std::unique_ptr<Cmd>>&b) {
        const bool noPlate=ieq(loadedMeta.category,"NO_PLATE"); const bool turn=hasCap(loadedMeta,"TURN_SIGNAL_COMPATIBLE");
        for(const auto&up:b){const Cmd&c=*up;
            if(noPlate){if(c.kind==Cmd::MOVE||c.kind==Cmd::STOP||c.kind==Cmd::WAIT_TARGET)return fail("NO_PLATE scenes cannot use motion or WAIT TARGET");if((c.kind==Cmd::SET||c.kind==Cmd::CLEAR)&& (c.zone!=Zone::NO_PLATELEDS||c.side!=Side::BOTH))return fail("NO_PLATE scenes may access only BOTH NO_PLATELEDS");}
            if(c.kind==Cmd::SET||c.kind==Cmd::CLEAR){if(c.zone==Zone::NO_PLATELEDS && c.side!=Side::BOTH)return fail("NO_PLATELEDS requires BOTH");if(c.zone==Zone::LENS&&c.selector.has)return fail("LENS does not accept selector");if(turn&&c.zone!=Zone::NO_PLATELEDS&&c.side!=Side::BOTH)return fail("TURN_SIGNAL_COMPATIBLE requires indicator LED operations to target BOTH");if(!noPlate && c.zone==Zone::NO_PLATELEDS&&!hasCap(loadedMeta,"SYNCED_COMPATIBLE"))return fail("NORMAL NO_PLATELEDS access requires SYNCED_COMPATIBLE");}
            if((c.kind==Cmd::MOVE||c.kind==Cmd::STOP)&&turn&&c.side!=Side::BOTH)return fail("TURN_SIGNAL_COMPATIBLE requires motion to target BOTH");
            if(!validateRestrictions(c.body)||!validateRestrictions(c.els))return false;
        } return true;
    }

    static constexpr size_t MAX_CONTROL_NESTING = 64;

    bool parseScript(const std::string&s){
        tree.clear(); code.clear(); std::vector<std::vector<std::unique_ptr<Cmd>>*> stacks{&tree}; std::vector<Cmd*> opens; bool finalEnd=false; size_t lineNo=0;
        std::istringstream in(s); std::string line;
        while(std::getline(in,line)){
            ++lineNo;
            if(lineNo>4096) return fail("SCRIPT exceeds 4096 lines");
            line=stripComment(line);
            if(line.empty()) continue;
            if(finalEnd)return fail("command appears after final SCRIPT END at line "+std::to_string(lineNo));
            if(ieq(line,"END")){if(stacks.size()==1){finalEnd=true;continue;}stacks.pop_back();opens.pop_back();continue;}
            if(ieq(line,"ELSE")){if(opens.empty()||opens.back()->kind!=Cmd::IF)return fail("ELSE without IF at line "+std::to_string(lineNo));stacks.pop_back();stacks.push_back(&opens.back()->els);continue;}
            std::unique_ptr<Cmd> c=parseCmd(line);if(!c)return false;Cmd*raw=c.get();stacks.back()->push_back(std::move(c));if(raw->kind==Cmd::IF||raw->kind==Cmd::WHILE||raw->kind==Cmd::FOR){if(opens.size()>=MAX_CONTROL_NESTING)return fail("control nesting limit exceeded");opens.push_back(raw);stacks.push_back(&raw->body);}
        }
        if(!finalEnd) return fail("SCRIPT must end with final END");
        if(!stacks.empty() && stacks.size()!=1) return fail("unclosed control block");
        if(tree.empty()) return fail("SCRIPT body is empty");
        if(!validateRestrictions(tree)) return false;
        return compileBlock(tree);
    }

    std::unique_ptr<Cmd> parseCmd(const std::string&x){
        auto c=std::make_unique<Cmd>();
        std::string e;
        const std::string line=trim(x);

        if(ieq(line,"WAIT TARGET")){c->kind=Cmd::WAIT_TARGET;return c;}
        if(line.size()>5 && ieq(line.substr(0,5),"WAIT ")){
            const std::string arg=trim(line.substr(5));
            size_t unitPos=arg.size();
            if(arg.size()>=2 && ieq(arg.substr(arg.size()-2),"ms")) unitPos=arg.size()-2;
            else if(arg.size()>=3 && ieq(arg.substr(arg.size()-3),"sec")) unitPos=arg.size()-3;
            const std::string number=trim(arg.substr(0,unitPos));
            const std::string unit=lower(trim(arg.substr(unitPos)));
            uint64_t n=0;
            if(!parseUnsignedInt(number,n,3600000ULL) || !(unit.empty()||unit=="ms"||unit=="sec")){
                fail("invalid WAIT command");return nullptr;
            }
            if(unit=="sec"){
                if(n>3600ULL){fail("WAIT seconds exceeds 3600sec");return nullptr;}
                n*=1000ULL;
            }
            c->kind=Cmd::WAIT;c->a=std::make_unique<Lit>(Value::num(static_cast<double>(n)));return c;
        }

        if(line.size()>5 && ieq(line.substr(0,5),"MOVE ")){
            size_t pos=5;std::string side,word;
            if(!takeWord(line,pos,side)||!parseSide(side,c->side)){fail("invalid MOVE side");return nullptr;}
            if(!takeWord(line,pos,word)||!ieq(word,"POS")){fail("invalid MOVE syntax");return nullptr;}
            const std::string rest=trim(line.substr(pos));
            const size_t speedAt=findKeywordTopLevel(rest,"SPEED");
            if(speedAt==std::string::npos){fail("invalid MOVE syntax");return nullptr;}
            const std::string posExpr=trim(rest.substr(0,speedAt));
            const std::string speedExpr=trim(rest.substr(speedAt+5));
            if(posExpr.empty()||speedExpr.empty()){fail("invalid MOVE expression");return nullptr;}
            c->kind=Cmd::MOVE;c->a=parseExpr(posExpr,e);
            if(e.empty()) c->b=parseExpr(speedExpr,e);
            if(!c->a||!c->b||!e.empty()){fail("invalid MOVE expression: "+e);return nullptr;}
            return c;
        }

        if(line.size()>5 && ieq(line.substr(0,5),"STOP ")){
            const std::string side=trim(line.substr(5));
            if(!parseSide(side,c->side)){fail("invalid STOP side");return nullptr;}
            c->kind=Cmd::STOP;return c;
        }
        if(ieq(line,"SHOW")){c->kind=Cmd::SHOW;return c;}

        if(line.size()>4 && ieq(line.substr(0,4),"LET ")){
            const std::string rest=trim(line.substr(4));
            const size_t eq=rest.find('=');
            if(eq==std::string::npos){fail("invalid LET syntax");return nullptr;}
            c->var=trim(rest.substr(0,eq));
            if(!identifier(c->var)){fail("invalid LET variable");return nullptr;}
            c->kind=Cmd::LET;c->a=parseExpr(trim(rest.substr(eq+1)),e);
            if(!c->a||!e.empty()){fail("invalid LET expression: "+e);return nullptr;}
            return c;
        }
        if(line.size()>3 && ieq(line.substr(0,3),"IF ")){
            c->kind=Cmd::IF;c->a=parseExpr(trim(line.substr(3)),e);
            if(!c->a||!e.empty()){fail("invalid IF expression: "+e);return nullptr;}return c;
        }
        if(line.size()>6 && ieq(line.substr(0,6),"WHILE ")){
            c->kind=Cmd::WHILE;c->a=parseExpr(trim(line.substr(6)),e);
            if(!c->a||!e.empty()){fail("invalid WHILE expression: "+e);return nullptr;}return c;
        }

        if(line.size()>=4 && ieq(line.substr(0,3),"FOR") && (line[3]==' '||line[3]=='(')){
            const std::string rest=trim(line.substr(3));
            if(rest.size()<3||rest.front()!='('||rest.back()!=')'){fail("invalid FOR syntax");return nullptr;}
            const std::string body=trim(rest.substr(1,rest.size()-2));
            std::vector<std::string> parts;
            if(!splitTop(body,';',parts)||parts.size()!=3){fail("invalid FOR expression");return nullptr;}
            std::vector<std::string> init,step;
            if(!splitTop(parts[0],'=',init)||init.size()!=2||!identifier(trim(init[0]))){fail("invalid FOR initializer");return nullptr;}
            c->kind=Cmd::FOR;c->var=trim(init[0]);c->a=parseExpr(init[1],e);
            if(!splitTop(parts[2],'=',step)||step.size()!=2||!ieq(trim(step[0]),c->var)){fail("FOR final expression must update loop variable");return nullptr;}
            c->b=parseExpr(parts[1],e);c->c=parseExpr(step[1],e);
            if(!c->a||!c->b||!c->c||!e.empty()){fail("invalid FOR expression: "+e);return nullptr;}
            return c;
        }

        const size_t firstSpace=line.find_first_of(" \t");
        if(firstSpace!=std::string::npos){
            const std::string verb=upper(line.substr(0,firstSpace));
            if(verb=="SET"||verb=="CLEAR"){
                size_t pos=firstSpace;std::string side;
                if(!takeWord(line,pos,side)||!parseSide(side,c->side)){
                    fail("invalid LED command syntax");return nullptr;
                }
                while(pos<line.size()&&std::isspace(static_cast<unsigned char>(line[pos])))++pos;
                const std::string zones[]={"FRONTLEDS","MAINLEDS","NO_PLATELEDS","BACKLEDS","LENS"};
                bool zoneFound=false;
                for(const std::string&zn:zones){
                    if(pos+zn.size()<=line.size() && ieq(line.substr(pos,zn.size()),zn)){
                        const size_t after=pos+zn.size();
                        if(after==line.size() || std::isspace(static_cast<unsigned char>(line[after])) || line[after]=='['){
                            parseZone(zn,c->zone);pos=after;zoneFound=true;break;
                        }
                    }
                }
                if(!zoneFound){fail("invalid LED command syntax");return nullptr;}
                c->kind=(verb=="SET")?Cmd::SET:Cmd::CLEAR;
                while(pos<line.size()&&std::isspace(static_cast<unsigned char>(line[pos])))++pos;
                if(pos<line.size()&&line[pos]=='['){
                    const size_t close=line.find(']',pos);
                    if(close==std::string::npos){fail("invalid selector");return nullptr;}
                    if(!parseSelector(line.substr(pos,close-pos+1),c->selector,e)){fail(e);return nullptr;}
                    pos=close+1;
                    while(pos<line.size()&&std::isspace(static_cast<unsigned char>(line[pos])))++pos;
                }
                if(c->zone==Zone::LENS&&c->selector.has){fail("LENS does not accept selector");return nullptr;}
                const std::string options=trim(line.substr(pos));
                if(c->kind==Cmd::CLEAR){
                    if(!options.empty()){fail("CLEAR accepts neither COLOR nor BRIGHTNESS");return nullptr;}
                    return c;
                }
                if(options.empty()||options.size()<6||!ieq(options.substr(0,6),"COLOR ")){fail("SET requires COLOR");return nullptr;}
                const std::string colorAndRest=trim(options.substr(6));
                const size_t brightAt=findKeywordTopLevel(colorAndRest,"BRIGHTNESS");
                const std::string colorExpr=trim(brightAt==std::string::npos?colorAndRest:colorAndRest.substr(0,brightAt));
                if(colorExpr.empty()){fail("SET requires COLOR");return nullptr;}
                if(brightAt!=std::string::npos){
                    const std::string btxt=trim(colorAndRest.substr(brightAt+10));
                    uint64_t b=0;
                    if(!parseUnsignedInt(btxt,b,255ULL)){fail("BRIGHTNESS must be 0..255");return nullptr;}
                    c->hasBrightness=true;c->brightness=static_cast<int>(b);
                }
                bool parsedLiteral=false;
                if(colorExpr.find(',')!=std::string::npos){
                    std::vector<std::string> rgb;
                    if(splitTop(colorExpr,',',rgb)&&rgb.size()==3){
                        uint64_t r=0,g=0,b=0;
                        const bool allNumeric=parseUnsignedInt(rgb[0],r,255ULL)&&parseUnsignedInt(rgb[1],g,255ULL)&&parseUnsignedInt(rgb[2],b,255ULL);
                        if(allNumeric){
                            c->a=std::make_unique<Lit>(Value::col(SceneColor{static_cast<uint8_t>(r),static_cast<uint8_t>(g),static_cast<uint8_t>(b)}));
                            parsedLiteral=true;
                        }
                    }
                }
                if(!parsedLiteral){
                    c->a=parseExpr(colorExpr,e);
                    if(!c->a||!e.empty()){fail("invalid COLOR expression: "+e);return nullptr;}
                }
                return c;
            }
        }
        fail("unsupported command: "+line);return nullptr;
    }

    bool resolveSelector(const SelectorSpec&s, std::vector<int32_t>&out){
        out.clear();if(!s.has)return true;if(!s.dynamic){out=s.indices;return true;}const auto it=env.find(s.variable);if(it==env.end())return fail("undefined selector variable: "+s.variable);if(it->second.type!=Value::NUMBER)return fail("dynamic selector requires NUMBER variable: "+s.variable);const double v=it->second.number;if(!std::isfinite(v)||std::floor(v)!=v)return fail("dynamic selector must be an integer: "+s.variable);out.push_back(static_cast<int32_t>(v));return true;
    }
    size_t zoneCount(Zone z) const {
        // Hardware counts belong to LED_CONFIG. SceneVM only asks the LED
        // adapter for the current count, so changing physical LED counts does
        // not require editing this VM implementation.
        return leds ? leds->zoneCount(z) : 0;
    }
    Side effectiveSide(Side authored) const {
        if (authored == Side::BOTH && hasCap(loadedMeta, "TURN_SIGNAL_COMPATIBLE") && context.triggeredSide != Side::NONE)
            return context.triggeredSide;
        return authored;
    }
    bool ledSet(const Cmd&c){std::vector<int32_t>idx;if(!resolveSelector(c.selector,idx))return false;if(!c.selector.has)idx.push_back(0);const Side side=effectiveSide(c.side);for(const int32_t n:idx){if(c.selector.has&&(zoneCount(c.zone)==0||n<1||static_cast<size_t>(n)>zoneCount(c.zone)))return fail("LED selector index out of physical range");LEDSelector s;s.hasSelector=c.selector.has;s.dynamic=c.selector.dynamic;s.index=c.selector.has?n:0;Value col;if(!eval(c.a,col)||col.type!=Value::COLOR)return fail("SET COLOR must evaluate to COLOR");if(!leds||!leds->set(side,c.zone,s,col.color,c.hasBrightness,static_cast<uint8_t>(c.brightness)))return fail("LED adapter SET failed");}return true;}
    bool ledClear(const Cmd&c){std::vector<int32_t>idx;if(!resolveSelector(c.selector,idx))return false;if(!c.selector.has)idx.push_back(0);const Side side=effectiveSide(c.side);for(const int32_t n:idx){if(c.selector.has&&(zoneCount(c.zone)==0||n<1||static_cast<size_t>(n)>zoneCount(c.zone)))return fail("LED selector index out of physical range");LEDSelector s;s.hasSelector=c.selector.has;s.dynamic=c.selector.dynamic;s.index=c.selector.has?n:0;if(!leds||!leds->clear(side,c.zone,s))return fail("LED adapter CLEAR failed");}return true;}

    bool step(){
        if(pc>=code.size()){st=VMState::COMPLETE;return true;}
        Instr&i=code[pc]; Value q;
        switch(i.op){
            case Instr::MOVE:{
                Value p,s;
                if(!eval(i.cmd->a,p)||!eval(i.cmd->b,s)||p.type!=Value::NUMBER||s.type!=Value::NUMBER)return fail("MOVE expressions must be NUMBER");
                if(!std::isfinite(p.number)||!std::isfinite(s.number)||p.number<0||p.number>100||s.number<1||s.number>100)return fail("MOVE POS/SPEED runtime range error");
                const Side side = effectiveSide(i.cmd->side);
                if(!motion||!motion->move(side,static_cast<uint8_t>(std::lround(p.number)),static_cast<uint8_t>(std::lround(s.number))))return fail("motion adapter MOVE failed");
                // WAIT TARGET refers to the most recently commanded motion side.
                // A MOVE BOTH therefore creates a BOTH target wait; a later
                // MOVE LEFT or MOVE RIGHT replaces that current target context.
                currentTargetSide=effectiveSide(i.cmd->side);
                lastMoveSide=currentTargetSide;
                lastMovePos=static_cast<uint8_t>(std::lround(p.number));
                lastMoveSpeed=static_cast<uint8_t>(std::lround(s.number));
                lastMotionWasMove=true;
                ++pc;
                return true;
            }
            case Instr::STOP:{const Side side=effectiveSide(i.cmd->side);if(!motion||!motion->stop(side))return fail("motion adapter STOP failed");lastMotionWasMove=false;currentTargetSide=Side::NONE;++pc;return true;}
            case Instr::WAIT:{if(!eval(i.cmd->a,q)||q.type!=Value::NUMBER||q.number<0||q.number>3600000||!std::isfinite(q.number))return fail("WAIT value out of range");waitUntil=lastNow+static_cast<uint32_t>(std::llround(q.number));++pc;st=VMState::WAITING_TIME;return true;}
            case Instr::WAIT_TARGET:
                if(!motion)return fail("motion adapter unavailable");
                if(currentTargetSide==Side::NONE)return fail("WAIT TARGET has no current motion target");
                if(motion->faulted())return fail(motion->faultText()?motion->faultText():"motion fault");
                waitSide=currentTargetSide;
                ++pc;
                st=VMState::WAITING_TARGET;
                return true;
            case Instr::SHOW:if(!leds||!leds->show())return fail("LED adapter SHOW failed");++pc;return true;
            case Instr::LET:if(!eval(i.cmd->a,q)||!assign(i.cmd->var,q))return false;++pc;return true;
            case Instr::SET:return ledSet(*i.cmd)?(void(++pc),true):false;
            case Instr::CLEAR:return ledClear(*i.cmd)?(void(++pc),true):false;
            case Instr::IF_FALSE:if(!eval(i.cmd->a,q)||q.type!=Value::BOOL)return fail("IF condition must be BOOL");if(!q.boolean)pc=i.jump;else ++pc;return true;
            case Instr::JUMP:pc=i.jump;return true;
            case Instr::WHILE_CHECK:if(!eval(i.cmd->a,q)||q.type!=Value::BOOL)return fail("WHILE condition must be BOOL");if(q.boolean)++pc;else pc=i.jump;return true;
            case Instr::FOR_INIT:{if(forStack.size()>=64)return fail("FOR nesting limit exceeded");if(!eval(i.cmd->a,q)||q.type!=Value::NUMBER)return fail("FOR initializer must be NUMBER");ForFrame f;f.var=lower(i.cmd->var);const auto it=env.find(f.var);if(it!=env.end()){f.hadOld=true;f.old=it->second;}forStack.push_back(f);env[f.var]=q;++pc;return true;}
            case Instr::FOR_CHECK:if(!eval(i.cmd->b,q)||q.type!=Value::BOOL)return fail("FOR condition must be BOOL");if(q.boolean)++pc;else pc=i.jump;return true;
            case Instr::FOR_STEP:if(!eval(i.cmd->c,q)||q.type!=Value::NUMBER)return fail("FOR final expression must be NUMBER");env[lower(i.cmd->var)]=q;++pc;return true;
            case Instr::FOR_END:{if(forStack.empty())return fail("FOR frame underflow");const ForFrame f=forStack.back();forStack.pop_back();if(f.hadOld)env[f.var]=f.old;else env.erase(f.var);++pc;return true;}
        } return fail("unknown VM instruction");
    }

    // Canonical .scene parsing uses deterministic manual field parsing.  The
    // ESP32 runtime parser stays lightweight in the scene-loading path.
    static bool parseAssignment(const std::string& raw, std::string& key, std::string& value) {
        const size_t eq = raw.find('=');
        if (eq == std::string::npos) return false;
        key = trim(raw.substr(0, eq));
        value = trim(raw.substr(eq + 1));
        // Metadata assignments may intentionally have an empty value,
        // specifically CAPABILITIES= for a normal scene. Individual fields
        // that require a value validate that requirement at their own parser.
        return identifier(key);
    }

    static bool parseUnsignedByte(const std::string& text, uint8_t& out) {
        const std::string s = trim(text);
        if (s.empty()) return false;
        unsigned value = 0;
        for (char ch : s) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
            value = value * 10U + static_cast<unsigned>(ch - '0');
            if (value > 255U) return false;
        }
        out = static_cast<uint8_t>(value);
        return true;
    }

    static bool parseColorAssignment(const std::string& raw, std::string& key, SceneColor& color) {
        std::string value;
        if (!parseAssignment(raw, key, value)) return false;
        std::vector<std::string> parts;
        if (!splitTop(value, ',', parts) || parts.size() != 3) return false;
        uint8_t r = 0, g = 0, b = 0;
        if (!parseUnsignedByte(parts[0], r) || !parseUnsignedByte(parts[1], g) || !parseUnsignedByte(parts[2], b)) return false;
        color = SceneColor{r, g, b};
        return true;
    }

    bool parseSceneFile(const std::string&text){
        std::vector<std::string>lines;
        std::istringstream in(text);
        std::string line;
        while(std::getline(in,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            lines.push_back(line);
        }
        while(!lines.empty()&&trim(lines.back()).empty())lines.pop_back();
        if(lines.empty()||!ieq(trim(lines[0]),"SCENE"))return fail("file must begin with SCENE");

        size_t colorsAt=std::numeric_limits<size_t>::max(),scriptAt=std::numeric_limits<size_t>::max();
        for(size_t i=0;i<lines.size();++i){
            const std::string u=upper(trim(lines[i]));
            if(u=="COLORS"){
                if(colorsAt!=std::numeric_limits<size_t>::max())return fail("duplicate COLORS section");
                colorsAt=i;
            }else if(u=="SCRIPT"){
                if(scriptAt!=std::numeric_limits<size_t>::max())return fail("duplicate SCRIPT section");
                scriptAt=i;
            }
        }
        if(colorsAt==std::numeric_limits<size_t>::max()||scriptAt==std::numeric_limits<size_t>::max()||!(0<colorsAt&&colorsAt<scriptAt))
            return fail("canonical SCENE requires SCENE, COLORS, SCRIPT in order");

        loadedMeta=SceneMeta{};
        std::unordered_map<std::string,bool>seen;
        for(size_t i=1;i<colorsAt;++i){
            const std::string raw=trim(lines[i]);
            if(raw.empty())continue;
            std::string key,value;
            if(!parseAssignment(raw,key,value))return fail("invalid SCENE metadata line");
            const std::string k=upper(key);
            if(seen[k])return fail("duplicate SCENE metadata field: "+k);
            seen[k]=true;
            if(k=="ID")loadedMeta.id=value;
            else if(k=="NAME")loadedMeta.name=value;
            else if(k=="DESC")loadedMeta.desc=value;
            else if(k=="CATEGORY")loadedMeta.category=upper(value);
            else if(k=="CAPABILITIES"){
                // CAPABILITIES is optional for NORMAL scenes. An empty
                // CAPABILITIES= line is intentionally valid and means the
                // scene has no special runtime capability.
                loadedMeta.caps.clear();
                const std::string capabilityText = trim(value);
                if (!capabilityText.empty()) {
                    std::vector<std::string>caps;
                    if(!splitTop(capabilityText,',',caps))return fail("invalid CAPABILITIES");
                    for(const std::string&cap:caps)if(!trim(cap).empty())loadedMeta.caps.push_back(upper(trim(cap)));
                }
            }else return fail("unknown SCENE metadata field: "+k);
        }
        if(loadedMeta.id.empty() || loadedMeta.name.empty() || loadedMeta.desc.empty() || loadedMeta.category.empty())return fail("missing required SCENE metadata");
        if(loadedMeta.category!="NORMAL" && loadedMeta.category!="NO_PLATE")return fail("invalid CATEGORY");

        std::unordered_map<std::string,bool>capSeen;
        for(const std::string&cap:loadedMeta.caps){
            if(capSeen[cap])return fail("duplicate capability: "+cap);
            capSeen[cap]=true;
            if(cap!="WELCOME_COMPATIBLE"&&cap!="TURN_SIGNAL_COMPATIBLE"&&cap!="SYNCED_COMPATIBLE")return fail("unknown capability: "+cap);
        }
        if(loadedMeta.category=="NO_PLATE"&&!loadedMeta.caps.empty())return fail("NO_PLATE scenes cannot declare capabilities");
        if(loadedMeta.category=="NO_PLATE"&&hasCap(loadedMeta,"WELCOME_COMPATIBLE"))return fail("invalid NO_PLATE capability");

        for(size_t i=colorsAt+1;i<scriptAt;++i){
            const std::string raw=trim(lines[i]);
            if(raw.empty())continue;
            std::string key;
            SceneColor color{0,0,0};
            if(!parseColorAssignment(raw,key,color))return fail("invalid COLORS line");
            const std::string k=lower(key);
            if(loadedMeta.colors.find(k)!=loadedMeta.colors.end())return fail("duplicate color slot: "+k);
            if(loadedMeta.colors.size()>=32)return fail("too many color slots");
            loadedMeta.colors[k]=color;
        }

        std::string script;
        for(size_t i=scriptAt+1;i<lines.size();++i){
            if(i>scriptAt+1)script+='\n';
            script+=lines[i];
        }
        tree.clear();
        code.clear();
        err.clear();
        if(!parseScript(script))return false;
        return true;
    }

public:
    void begin(MotionPort*m,LEDPort*l)override{motion=m;leds=l;st=VMState::IDLE;err.clear();}
    VMResult load(const char*text,size_t length)override{
        if(!text||length==0){err="empty scene";st=VMState::ERROR;return VMResult::INVALID;}st=VMState::IDLE;err.clear();tree.clear();code.clear();loadedMeta=SceneMeta{};
        const std::string s(text,length);bool ok=false;const std::string t=trim(s);if(t.rfind("SCENE",0)==0||t.rfind("scene",0)==0)ok=parseSceneFile(s);else { // Script-only is retained only as a host test convenience; canonical .scene is preferred.
            loadedMeta.category="NORMAL";ok=parseScript(s);
        }
        loadResult=ok?VMResult::OK:VMResult::INVALID;return loadResult;
    }
    VMResult start(const SceneContext&c=SceneContext{})override{
        if(loadResult!=VMResult::OK || code.empty()) return VMResult::INVALID;
        if(active()) return VMResult::BUSY;
        context=c; env.clear();
        for(const auto&kv:loadedMeta.colors) env[kv.first]=Value::col(kv.second);
        forStack.clear(); pc=0; timeMs=0; deltaMs=0; lastNow=0; waitUntil=0; waitSide=Side::BOTH; currentTargetSide=Side::NONE; clockStarted=false; lastMoveSide=Side::NONE; lastMovePos=0; lastMoveSpeed=100; lastMotionWasMove=false; st=VMState::RUNNING;
        return VMResult::OK;
    }
    void update(uint32_t nowMs)override{
        if(st==VMState::IDLE||st==VMState::COMPLETE||st==VMState::ERROR||st==VMState::PAUSED)return;
        if(!clockStarted){clockStarted=true;lastNow=nowMs;deltaMs=0;}else{deltaMs=static_cast<uint32_t>(nowMs-lastNow);lastNow=nowMs;timeMs+=deltaMs;}
        if(st==VMState::WAITING_TIME){if(static_cast<uint32_t>(nowMs-waitUntil)<0x80000000u)st=VMState::RUNNING;else return;}
        if(st==VMState::WAITING_TARGET){if(!motion){fail("motion adapter unavailable");return;}if(motion->faulted()){fail(motion->faultText()?motion->faultText():"motion fault");return;}if(!motion->targetReached(waitSide))return;st=VMState::RUNNING;}
        if(st!=VMState::RUNNING)return;
        // A bounded cooperative burst prevents a zero-wait/zero-progress scene
        // from monopolizing the MCU update loop. Waiting yields immediately.
        for(size_t n=0;n<10000&&st==VMState::RUNNING;++n){if(!step())return;}
        if(st==VMState::RUNNING&&pc<code.size())fail("VM instruction budget exceeded in one update");
    }
    void stop()override{if(motion)motion->stop(Side::BOTH);st=VMState::IDLE;pc=0;forStack.clear();currentTargetSide=Side::NONE;lastMotionWasMove=false;lastMoveSide=Side::NONE;}
    void pause()override{
        if(!active()) return;
        // Pause must also freeze an in-flight motor command. Remember the
        // latest MOVE so resume can restore the motion target before execution
        // continues. This keeps a paused scene coherent while a foreground
        // turn-signal scene temporarily owns the same physical motors.
        if(motion && lastMotionWasMove) motion->stop(Side::BOTH);
        st=VMState::PAUSED;
    }
    VMResult resume()override{
        if(st!=VMState::PAUSED)return VMResult::INVALID;
        if(motion && lastMotionWasMove && lastMoveSide!=Side::NONE) {
            if(!motion->move(lastMoveSide,lastMovePos,lastMoveSpeed)) {
                return VMResult::FAILED;
            }
        }
        st=VMState::RUNNING;
        return VMResult::OK;
    }
    VMState state()const override{return st;}
    bool active()const override{return st==VMState::RUNNING||st==VMState::WAITING_TIME||st==VMState::WAITING_TARGET||st==VMState::PAUSED;}
    bool paused()const override{return st==VMState::PAUSED;}
    const char*lastError()const override{return err.c_str();}
};
}
SceneVM* createSceneVMV1(){return new VMImpl();}
}
