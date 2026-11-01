#include <pgn/native_report_audit.h>
#include <iostream>
#include <stdexcept>
int main(){
 using namespace pgn::core;int n=0;auto check=[&](bool b){++n;if(!b)throw std::runtime_error("clock check "+std::to_string(n));};
 NativeAuditJob legacy;legacy.issued=3;legacy.deadline=15;
 check(!NativeAuditAssignmentExpired(legacy,14,1000000));check(NativeAuditAssignmentExpired(legacy,15,0));
 NativeAuditJob timed;timed.issued=3;timed.deadline=0;timed.time_lease=auditTimeLease(1000,NativeAuditLeaseSeconds);check(timed.time_lease.has_value());
 check(!NativeAuditAssignmentExpired(timed,1000000000,1000+NativeAuditLeaseSeconds-1));check(NativeAuditAssignmentExpired(timed,4,1000+NativeAuditLeaseSeconds));
 const auto body=NativeTimedAuditAssignmentBody();const std::string text{body.begin(),body.end()};check(text=="{\"lease_seconds\":86400,\"schema\":\"PGN_AUDIT_ASSIGNMENT_V2\"}");
 check(!auditTimeLease(INT64_MAX,1));check(!auditTimeLease(-1,1));check(!auditTimeLease(0,0));
 std::cout<<"PASS "<<n<<" native assignment lease/body checks; no consensus deployment claimed\n";
}
