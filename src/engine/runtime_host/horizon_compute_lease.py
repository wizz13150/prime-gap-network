"""Interpret the clock explicitly declared by an owned Core ticket snapshot.

This is a computation lease only. It neither expires submitted reports nor
removes pending audits, and never substitutes one clock for another.
"""


def _counter(value, field):
    number = value.get(field)
    if type(number) is not int or not 0 <= number <= (1 << 63) - 1:
        raise ValueError("Core ticket clock field is missing or invalid: " + field)
    return number


def computation_lease_active(page, lease):
    if lease.get("state") != "assigned":
        return False
    height = _counter(page, "height")
    issued_height = _counter(lease, "issued")
    if height < issued_height:
        raise ValueError("Ticket was issued after this Core snapshot")
    clock = lease.get("deadline_clock", "ledger_height")
    if clock == "ledger_height":
        issued, now, deadline = issued_height, height, _counter(lease, "deadline")
    elif clock == "median_chain_time":
        issued = _counter(lease, "issued_time")
        now = _counter(page, "median_time_past")
        deadline = _counter(lease, "deadline_time")
    else:
        raise ValueError("Unsupported Core ticket deadline clock")
    if deadline <= issued or now < issued:
        raise ValueError("Core ticket deadline is inconsistent with its issue time")
    return now < deadline
