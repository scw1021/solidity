contract SmokeTest {
}
// ====
// compileViaYul: also
// ----
// constructor()
// smokeTest2 -> 0x2345
// ~ smokeTest20
// ~ smokeTest21
// ~ smokeTest22
// smokeTest2 -> 0x2345 # commentA #
// ~ smokeTest20
// ~ smokeTest21
// ~ smokeTest22
// smokeTest2 # commentA # -> 0x2345 # commentA #
// ~ smokeTest20
// ~ smokeTest21
// ~ smokeTest22
// smokeTest2 # commentA # -> 0x2345
// ~ smokeTest20
// ~ smokeTest21
// ~ smokeTest22
