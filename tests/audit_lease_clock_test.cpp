#include "pgn/audit_lease_clock.h"
#include <stdexcept>
#include <iostream>
int main(){using namespace pgn::core;int n=0;auto check=[&](bool b){++n;if(!b)throw std::runtime_error(std::to_string(n));};auto lease=auditTimeLease(100000,86400);check(bool(lease));check(!lease->expired(200,100001));check(!lease->expired(1000000000,110000));check(!lease->expired(2000000000,186399));check(lease->expired(201,186400));check(lease->expired(201,186401));check(!auditTimeLease(-1,10));check(!auditTimeLease(1,0));check(!auditTimeLease(INT64_MAX,1));AuditAssignmentLease old{AuditAssignmentLease::Clock::LedgerHeight,100,244};check(!old.expired(243,INT64_MAX));check(old.expired(244,0));std::cout<<"PASS "<<n<<" audit assignment clock checks; no report retention/consensus activation claimed\n";}
