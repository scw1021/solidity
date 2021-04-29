pragma abicoder v2;

struct Item {uint x;}
library L {
    event Ev(Item);
    function o() public { emit L.Ev(Item(1)); }
}
contract C {
    function f() public {
        L.o();
    }
}
// ====
// compileViaYul: also
// ----
// library: L
// f() ->
// - log[0]
// -   data=0000000000000000000000000000000000000000000000000000000000000001
// -   topic[0]=d8995181daeaf0ad90300384c4cef95adc251321ff8e5194f1e64733dcbd2746
