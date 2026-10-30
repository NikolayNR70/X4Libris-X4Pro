#include "CssCascade.h"
#include "CssParser.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace CssCascade {
namespace {
std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}
bool ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='-' || static_cast<unsigned char>(c)>=128; }
bool equal(std::string_view a, std::string_view b) {
  if(a.size()!=b.size()) return false;
  for(size_t i=0;i<a.size();++i) if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}
struct Part { std::string_view token; bool child=false; };
bool split(std::string_view s, std::array<Part,32>& parts, size_t& n) {
  s=trim(s); n=0; bool child=false;
  while(!s.empty()) {
    size_t end=0; while(end<s.size() && !std::isspace(static_cast<unsigned char>(s[end])) && s[end]!='>') ++end;
    if(!end || n==parts.size()) return false;
    auto token=s.substr(0,end); size_t i=0;
    if(token[i]=='*') ++i;
    else if(token[i]!='.' && token[i]!='#') { while(i<token.size() && ident(token[i])) ++i; if(!i) return false; }
    while(i<token.size()) {
      if(token[i]!='.' && token[i]!='#') return false;
      size_t start=++i; while(i<token.size() && ident(token[i])) ++i; if(start==i) return false;
    }
    parts[n++]={token,child}; s=trim(s.substr(end)); child=false;
    if(!s.empty() && s.front()=='>') { child=true; s=trim(s.substr(1)); if(s.empty()) return false; }
  }
  return n>0;
}
bool hasClass(std::string_view classes, std::string_view name) {
  while(!(classes=trim(classes)).empty()) {
    size_t n=0; while(n<classes.size() && !std::isspace(static_cast<unsigned char>(classes[n]))) ++n;
    if(classes.substr(0,n)==name) return true;
    classes.remove_prefix(n);
  }
  return false;
}
bool matches(std::string_view token, const Element& e) {
  size_t i=0;
  if(token[i]=='*') ++i;
  else if(token[i]!='.' && token[i]!='#') {
    while(i<token.size() && ident(token[i])) ++i;
    if(!equal(token.substr(0,i),e.tag)) return false;
  }
  while(i<token.size()) {
    char type=token[i++]; size_t start=i; while(i<token.size() && ident(token[i])) ++i;
    auto name=token.substr(start,i-start);
    if(type=='.' ? !hasClass(e.classes,name) : name!=e.id) return false;
  }
  return true;
}
bool matchPath(const std::array<Part,32>& p, size_t count, const std::vector<Element>& path) {
  if(path.empty() || path.size()>64) return false;
  std::array<bool,64> previous{},current{};
  for(size_t part=0;part<count;++part) {
    bool prefix=false;
    for(size_t el=0;el<path.size();++el) {
      current[el]=matches(p[part].token,path[el]) && (part==0 || (p[part].child?(el>0 && previous[el-1]):prefix));
      prefix=prefix || previous[el];
    }
    previous=current;
  }
  return previous[path.size()-1];
}
// Explicit field list avoids compiler-specific bitfield representations.
#define PROPERTIES(X) \
 X(textAlign) X(fontStyle) X(fontWeight) X(textDecoration) X(textIndent) \
 X(marginTop) X(marginBottom) X(marginLeft) X(marginRight) \
 X(paddingTop) X(paddingBottom) X(paddingLeft) X(paddingRight) \
 X(imageHeight) X(imageWidth) X(display) X(verticalAlign) X(listStyleNone) \
 X(pageBreakBefore) X(pageBreakAfter) X(lineHeight) X(fontSizeMultiplier) \
 X(cssFloat) X(smallCaps) X(colorTransparent) X(opacityZero) X(visibilityHidden)
void keywordStyle(const std::string& name, CssStyle& out, const CssStyle& source) {
#define FIELD(field,css) if(name==css) {out.field=source.field;out.defined.field=1;}
  FIELD(textAlign,"text-align") FIELD(fontStyle,"font-style") FIELD(fontWeight,"font-weight")
  FIELD(textDecoration,"text-decoration") FIELD(textIndent,"text-indent") FIELD(fontSizeMultiplier,"font-size")
  FIELD(smallCaps,"font-variant") FIELD(display,"display") FIELD(verticalAlign,"vertical-align")
  FIELD(cssFloat,"float")
#undef FIELD
  if(name=="line-height") {out.lineHeightMultiplier=source.lineHeightMultiplier;out.defined.lineHeight=1;}
#define SIDE(field,css) if(name==css || name==css.substr(0,css.find('-'))) {out.field=source.field;out.defined.field=1;}
  SIDE(marginTop,std::string("margin-top")) SIDE(marginBottom,std::string("margin-bottom"))
  SIDE(marginLeft,std::string("margin-left")) SIDE(marginRight,std::string("margin-right"))
  SIDE(paddingTop,std::string("padding-top")) SIDE(paddingBottom,std::string("padding-bottom"))
  SIDE(paddingLeft,std::string("padding-left")) SIDE(paddingRight,std::string("padding-right"))
#undef SIDE
}
CssStyle inherited(const CssStyle& p) {
  CssStyle s;
  s.hyphens=p.hyphens;
#define INHERIT(field) s.field=p.field;s.defined.field=p.defined.field;
  INHERIT(textAlign) INHERIT(fontStyle) INHERIT(fontWeight) INHERIT(textDecoration)
  INHERIT(textIndent) INHERIT(fontSizeMultiplier) INHERIT(smallCaps)
#undef INHERIT
  s.lineHeightMultiplier=p.lineHeightMultiplier;s.defined.lineHeight=p.defined.lineHeight;
  s.computedLineHeightEm=p.computedLineHeightEm;s.computedLineHeightPx=p.computedLineHeightPx;
  return s;
}
bool readString(FsFile& f, std::string& s) {
  uint16_t n=0; if(f.read(reinterpret_cast<uint8_t*>(&n),2)!=2 || n>1024 || f.position()+n>f.size()) return false;
  s.resize(n); return !n || f.read(reinterpret_cast<uint8_t*>(&s[0]),n)==n;
}
}
bool usable(std::string_view s) {std::array<Part,32> p;size_t n;return split(s,p,n);}
int specificity(std::string_view s, const std::vector<Element>& path) {
  std::array<Part,32> p;size_t n;
  if(path.empty() || !split(s,p,n) || !matchPath(p,n,path)) return -1;
  int score=0;
  for(size_t j=0;j<n;++j) {
    const auto t=p[j].token;
    if(t[0]!='*' && t[0]!='.' && t[0]!='#') ++score;
    for(char c:t) {if(c=='.') score+=256; if(c=='#') score+=65536;}
  }
  return score;
}
CssStyle resolve(FsFile& records, const std::vector<Element>& path, const CssStyle& parent,
                 const std::string& inlineStyle, float emSize, uint32_t count) {
  CssStyle result=inherited(parent);
  std::array<uint64_t,27> ranks{};
  uint64_t hyphensRank=0;
  std::string fontValue,lineValue;
  bool ownFont=false,ownLine=false;
  uint32_t order=0;
  auto declaration=[&](std::string_view d,int spec) {
    size_t colon=d.find(':'); if(colon==d.npos) return;
    std::string name(trim(d.substr(0,colon))),value(trim(d.substr(colon+1)));
    for(char& c:name)c=std::tolower(static_cast<unsigned char>(c));
    for(char& c:value)c=std::tolower(static_cast<unsigned char>(c));
    bool important=false;size_t pos=value.rfind("!important");
    if(pos!=value.npos && trim(std::string_view(value).substr(pos+10)).empty()) {important=true;value=std::string(trim(std::string_view(value).substr(0,pos)));}
    const uint64_t rank=(uint64_t(important)<<63) | (uint64_t(spec)<<32) | ++order;
    if(name=="hyphens" || name=="-webkit-hyphens" || name=="-epub-hyphens") {
      CssHyphens mode;
      if(value=="none") mode=CssHyphens::None;
      else if(value=="auto") mode=CssHyphens::Auto;
      else if(value=="manual" || value=="initial") mode=CssHyphens::Manual;
      else if(value=="inherit" || value=="unset") mode=parent.hyphens;
      else return;
      if(rank>=hyphensRank) {result.hyphens=mode;hyphensRank=rank;}
      return;
    }
    CssStyle candidate=CssParser::parseInlineStyle(std::string(d));
    if(value=="inherit" || value=="initial" || value=="unset" || (name=="line-height" && value=="normal")) {
      const bool inherit=value=="inherit" || (value=="unset" && (name=="text-align" || name=="text-indent" || name=="font-size" || name=="line-height" || name=="font-style" || name=="font-weight" || name=="font-variant"));
      keywordStyle(name,candidate,inherit?parent:CssStyle{});
    }
    // A visible declaration must cancel an earlier transparent/hidden declaration
    // even though the compact monochrome index omits visible colour values.
    if(name=="color" || name=="-webkit-text-fill-color") candidate.defined.colorTransparent=1;
    if(name=="opacity") candidate.defined.opacityZero=1;
    if(name=="visibility") candidate.defined.visibilityHidden=1;
    size_t index=0;
#define APPLY(field) if(candidate.defined.field && rank>=ranks[index]) {result.field=candidate.field;result.defined.field=1;ranks[index]=rank;} ++index;
    APPLY(textAlign) APPLY(fontStyle) APPLY(fontWeight) APPLY(textDecoration) APPLY(textIndent)
    APPLY(marginTop) APPLY(marginBottom) APPLY(marginLeft) APPLY(marginRight)
    APPLY(paddingTop) APPLY(paddingBottom) APPLY(paddingLeft) APPLY(paddingRight)
    APPLY(imageHeight) APPLY(imageWidth) APPLY(display) APPLY(verticalAlign) APPLY(listStyleNone)
    APPLY(pageBreakBefore) APPLY(pageBreakAfter)
    if(candidate.defined.lineHeight && rank>=ranks[index]) {result.defined.lineHeight=1;ranks[index]=rank;lineValue=value;ownLine=true;} ++index;
    if(candidate.defined.fontSizeMultiplier && rank>=ranks[index]) {result.fontSizeMultiplier=candidate.fontSizeMultiplier;result.defined.fontSizeMultiplier=1;ranks[index]=rank;fontValue=value;ownFont=true;} ++index;
    APPLY(cssFloat) APPLY(smallCaps) APPLY(colorTransparent) APPLY(opacityZero) APPLY(visibilityHidden)
#undef APPLY
  };
  std::string group,decl;
  for(uint32_t record=0;record<count;++record) {
    if(!records || !readString(records,group) || !readString(records,decl)) {result.cascadeValid=false;return result;}
    int best=-1;size_t start=0;
    for(size_t i=0;i<=group.size();++i) if(i==group.size() || group[i]==',') {best=std::max(best,specificity(std::string_view(group).substr(start,i-start),path));start=i+1;}
    if(best>=0) declaration(decl,best);
  }
  if(records && records.position()!=records.size()) {result.cascadeValid=false;return result;}
  size_t start=0;
  for(size_t i=0;i<=inlineStyle.size();++i) if(i==inlineStyle.size() || inlineStyle[i]==';') {declaration(std::string_view(inlineStyle).substr(start,i-start),1<<24);start=i+1;}
  const float parentSize=parent.hasFontSizeMultiplier()?parent.fontSizeMultiplier:1.f;
  if(ownFont && (fontValue.find('%')!=fontValue.npos || (fontValue.size()>2 && fontValue.substr(fontValue.size()-2)=="em" && fontValue.find("rem")==fontValue.npos) || fontValue=="smaller" || fontValue=="larger")) result.fontSizeMultiplier*=parentSize;
  if(!ownFont && !path.empty()) {
    float ua=1.f;
    const auto& tag=path.back().tag;
    if(tag=="h1") ua=1.6f;else if(tag=="h2") ua=1.4f;else if(tag=="h3") ua=1.2f;
    if(ua!=1.f) {result.fontSizeMultiplier=parentSize*ua;result.defined.fontSizeMultiplier=1;}
  }
  result.computedFontSize=true;result.fontSizeDeclared=ownFont;
  result.inlineFontMultiplier=result.fontSizeMultiplier/std::max(.01f,parentSize);
  if(ownLine && lineValue!="inherit" && lineValue!="unset") {
    result.computedLineHeightEm=result.computedLineHeightPx=0;
    char* end=nullptr;const float v=std::strtof(lineValue.c_str(),&end);
    if(end!=lineValue.c_str() && v>0) {
      const std::string unit=std::string(trim(end));
      if(unit.empty()) result.computedLineHeightEm=v;
      else if(unit=="px") result.computedLineHeightPx=v;
      else if(unit=="pt") result.computedLineHeightPx=v*4.f/3.f;
      else if(unit=="em" || unit=="%") result.computedLineHeightPx=v*(unit=="%"?.01f:1.f)*emSize*result.fontSizeMultiplier;
    }
  }
  return result;
}
}
