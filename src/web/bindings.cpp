#include "veu/subnet.hpp"
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <limits>
using emscripten::val;
using namespace veu::subnet;
namespace {
std::string str(val v, const char* key, std::string fallback = "") {
 auto x=v[key]; if(x.isUndefined())return fallback;
 if(x.typeOf().as<std::string>()!="string")throw Error(std::string(key)+" must be text.");
 auto s=x.as<std::string>(); if(s.size()>4096)throw Error("Field too long."); return s;
}
bool flag(val v,const char* key) {auto x=v[key];if(x.isUndefined())return false;if(x.typeOf().as<std::string>()!="boolean")throw Error("Invalid flag.");return x.as<bool>();}
val list(val v,const char* key) {auto a=v[key];if(a.isUndefined())return val::array();if(!val::global("Array").call<bool>("isArray",a)||a["length"].as<unsigned>()>1000)throw Error("List must contain at most 1000 rows.");return a;}
void address(val out,const char* key,const std::optional<Address>& a){out.set(key,a?format(*a):"");}
val execute_request(val q) {
 auto op=str(q,"op");auto out=val::object();
 if(op=="address") {auto a=parse_address(str(q,"address"));auto f=a.family==Family::ipv4?"ipv4":"ipv6";if(str(q,"family",f)!=f)throw Error("Address family does not match.");out.set("address",format(a));out.set("family",f);return out;}
 if(op=="calculate") {
 auto c=calculate(str(q,"address"),str(q,"prefix"));
 out.set("entered",format(c.entered));out.set("network",format(c.prefix));out.set("prefix",std::to_string(c.prefix.length));out.set("family",c.entered.family==Family::ipv4?"ipv4":"ipv6");out.set("total",c.total.str());out.set("last",format(c.last));out.set("offset",c.offset.str());
 address(out,"mask",c.mask);address(out,"wildcard",c.wildcard);address(out,"broadcast",c.broadcast);address(out,"firstHost",c.first_host);address(out,"lastHost",c.last_host);out.set("capacity",c.capacity?c.capacity->str():"");out.set("note",c.capacity_note);out.set("classification",classify(c.prefix).summary);return out;
 }
 if(op=="split") {auto parent=parse_cidr(str(q,"parent"));auto child=parse_prefix(str(q,"child"),parent.network.family);auto index=Integer::decimal(str(q,"index","0"));out.set("count",split_count(parent,child).str());out.set("index",index.str());auto rows=val::array();for(auto &p:split_page(parent,child,index,50))rows.call<void>("push",format(p));out.set("rows",rows);out.set("parent",format(parent));return out;}
 if(op=="allocate") {
 auto parent=parse_cidr(str(q,"parent"),true);std::vector<Request> requests;auto a=list(q,"requirements");
 for(unsigned i=0;i<a["length"].as<unsigned>();++i){auto v=a[i];Request r;r.id=str(v,"id");r.name=str(v,"name");auto seq=str(v,"sequence",std::to_string(i));auto seq_value=Integer::decimal(seq);if(seq_value>Integer(std::numeric_limits<std::uint64_t>::max()))throw Error("Insertion sequence exceeds 64 bits.");r.sequence=std::stoull(seq);auto kind=str(v,"kind");
 if(kind=="lan")r.kind=RequestKind::ipv4_lan;else if(kind=="ptp")r.kind=RequestKind::ipv4_point_to_point;else if(kind=="host")r.kind=RequestKind::ipv4_host_route;else if(kind=="ipv6")r.kind=RequestKind::ipv6_prefix;else throw Error("Unknown requirement role.");
 if(kind=="ipv6")r.child_prefix=parse_prefix(str(v,"quantity"),Family::ipv6);else r.hosts=Integer::decimal(str(v,"quantity"));auto assigned=str(v,"assigned");if(!assigned.empty())r.assigned=parse_cidr(assigned,true);r.pinned=flag(v,"pinned");requests.push_back(r);}
 std::vector<Reservation> reservations;auto b=list(q,"reservations");for(unsigned i=0;i<b["length"].as<unsigned>();++i){auto v=b[i];reservations.push_back({str(v,"id"),str(v,"name"),parse_cidr(str(v,"cidr"),true)});}
 auto r=allocate(parent,requests,reservations,flag(q,"reallocate"));if(r.reservations_overlap)throw Error("Reservations must not overlap.");auto alloc=val::array();Integer used;
 for(auto &v:r.allocations){auto row=val::object();row.set("id",v.id);row.set("cidr",format(v.prefix));row.set("preserved",v.preserved);row.set("pinned",v.pinned);row.set("addresses",size(v.prefix).str());alloc.call<void>("push",row);used=used+size(v.prefix);}
 auto missing=val::array();for(auto &v:r.unallocated){auto row=val::object();row.set("id",v.id);row.set("reason",v.reason);missing.call<void>("push",row);}
 auto blocks=val::array();for(auto &v:r.free.cidrs)blocks.call<void>("push",format(v));out.set("allocations",alloc);out.set("unallocated",missing);out.set("partial",r.partial);out.set("free",r.free.count.str());out.set("reserved",r.reserved.count.str());out.set("allocated",used.str());out.set("largestFree",r.largest_free?format(*r.largest_free):"");out.set("freeBlocks",blocks);return out;
 }
 throw Error("Unknown operation.");
}
std::string execute(const std::string& json){auto reply=val::object();try {if(json.size()>1024*1024)throw Error("Request exceeds 1 MiB.");auto q=val::global("JSON").call<val>("parse",json);reply.set("result",execute_request(q));reply.set("ok",true);}catch(const std::exception& e){reply.set("ok",false);reply.set("error",std::string(e.what()));}return val::global("JSON").call<std::string>("stringify",reply);}
}
EMSCRIPTEN_BINDINGS(veu_subnet) {emscripten::function("execute",&execute);}
