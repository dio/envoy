The hostname-aware dynamic-module cluster host callback now creates independent runtime hosts when
logical hosts share a socket address. Modules retain the returned host pointers as endpoint identity;
the legacy address-only callback retains address-based deduplication.
