#ifndef SIM_HIERARCHY_H
#define SIM_HIERARCHY_H

// Variadic macro to simplify accessing PGM core signals through sim_top wrapper
// Usage: PGM_SIGNAL(color_ram, ram_l) -> sim_top__DOT__pgm_inst__DOT__color_ram__DOT__ram_l
// Usage: PGM_SIGNAL(cpu_word_addr) -> sim_top__DOT__pgm_inst__DOT__cpu_word_addr

#define _PGM_CONCAT_HELPER(...) _PGM_CONCAT_IMPL(__VA_ARGS__, , , , , , , , , )
#define _PGM_CONCAT_IMPL(a, b, c, d, e, f, g, h, i, ...)                                                                                    \
    a##__DOT__##b##__DOT__##c##__DOT__##d##__DOT__##e##__DOT__##f##__DOT__##g##__DOT__##h##__DOT__##i

#define _PGM_REMOVE_TRAILING_DOTS(x) _PGM_CLEAN_##x
#define _PGM_CLEAN_sim_top__DOT__pgm_inst__DOT____DOT____DOT____DOT____DOT____DOT____DOT____DOT__ sim_top__DOT__pgm_inst
#define _PGM_CLEAN_sim_top__DOT__pgm_inst__DOT__a__DOT____DOT____DOT____DOT____DOT____DOT____DOT__ sim_top__DOT__pgm_inst__DOT__a
#define _PGM_CLEAN_sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT____DOT____DOT____DOT____DOT____DOT__ sim_top__DOT__pgm_inst__DOT__a__DOT__b
#define _PGM_CLEAN_sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT__c__DOT____DOT____DOT____DOT____DOT__                                         \
    sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT__c
#define _PGM_CLEAN_sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT__c__DOT__d__DOT____DOT____DOT____DOT__                                        \
    sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT__c__DOT__d
#define _PGM_CLEAN_sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT__c__DOT__d__DOT__e__DOT____DOT____DOT__                                       \
    sim_top__DOT__pgm_inst__DOT__a__DOT__b__DOT__c__DOT__d__DOT__e

// Simpler approach using token pasting
#define PGM_SIGNAL_1(a) sim_top__DOT__pgm_inst__DOT__##a
#define PGM_SIGNAL_2(a, b) sim_top__DOT__pgm_inst__DOT__##a##__DOT__##b
#define PGM_SIGNAL_3(a, b, c) sim_top__DOT__pgm_inst__DOT__##a##__DOT__##b##__DOT__##c
#define PGM_SIGNAL_4(a, b, c, d) sim_top__DOT__pgm_inst__DOT__##a##__DOT__##b##__DOT__##c##__DOT__##d
#define PGM_SIGNAL_5(a, b, c, d, e) sim_top__DOT__pgm_inst__DOT__##a##__DOT__##b##__DOT__##c##__DOT__##d##__DOT__##e

// Count arguments macro
#define _PGM_GET_ARG_COUNT(...) _PGM_GET_ARG_COUNT_IMPL(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define _PGM_GET_ARG_COUNT_IMPL(_1, _2, _3, _4, _5, N, ...) N

// Main macro that dispatches to the correct arity version
#define PGM_SIGNAL(...) _PGM_SIGNAL_DISPATCH(_PGM_GET_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)
#define _PGM_SIGNAL_DISPATCH(N, ...) _PGM_SIGNAL_CONCAT(PGM_SIGNAL_, N)(__VA_ARGS__)
#define _PGM_SIGNAL_CONCAT(a, b) a##b

#define G_PGM_SIGNAL(...) gSimCore.mTop->rootp->_PGM_SIGNAL_DISPATCH(_PGM_GET_ARG_COUNT(__VA_ARGS__), __VA_ARGS__)

#endif // SIM_HIERARCHY_H
