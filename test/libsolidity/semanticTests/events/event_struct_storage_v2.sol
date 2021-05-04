pragma abicoder v2;
contract C {
    struct S { uint a; }
    event E(S);
    S s;
    function createEvent(uint x) public {
        s.a = x;
        emit E(s);
    }
}
// ====
// compileViaYul: also
// ----
// createEvent(uint256): 42 ->
// ~ emit <anonymous>: #0xdb7c15eb416c7028693224ffb718d13661df9f900b8db24786c57612ee461dd4, 0x2a
