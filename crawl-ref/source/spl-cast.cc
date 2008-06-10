/*
 *  File:       spl-cast.cc
 *  Summary:    Spell casting and miscast functions.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 *
 *  Change History (most recent first):
 *
 *      <4>      1/02/00        jmf             changed values, marked //jmf:
 *      <3>      6/13/99        BWR             Added Staff auto identify code
 *      <2>      5/20/99        BWR             Added some screen redraws
 *      <1>      -/--/--        LRH             Created
 */

#include "AppHdr.h"

#include <sstream>
#include <iomanip>

#include "spl-cast.h"

#include "externs.h"

#include "beam.h"
#include "cloud.h"
#include "describe.h"
#include "effects.h"
#include "fight.h"
#include "food.h"
#include "format.h"
#include "initfile.h"
#include "invent.h"
#include "it_use2.h"
#include "item_use.h"
#include "itemname.h"
#include "itemprop.h"
#include "macro.h"
#include "menu.h"
#include "message.h"
#include "misc.h"
#include "monplace.h"
#include "monstuff.h"
#include "mutation.h"
#include "ouch.h"
#include "player.h"
#include "quiver.h"
#include "religion.h"
#include "skills.h"
#include "skills2.h"
#include "spells1.h"
#include "spells2.h"
#include "spells3.h"
#include "spells4.h"
#include "spl-book.h"
#include "spl-util.h"
#include "state.h"
#include "stuff.h"
#include "transfor.h"
#include "tutorial.h"
#include "view.h"
#include "xom.h"

#ifdef DOS
#include <conio.h>
#endif

// This determines how likely it is that more powerful wild magic
// effects will occur.  Set to 100 for the old probabilities (although
// the individual effects have been made much nastier since then).
#define WILD_MAGIC_NASTINESS 150

static bool _surge_identify_boosters(spell_type spell)
{
    const unsigned int typeflags = get_spell_disciplines(spell);
    if ( (typeflags & SPTYP_FIRE) || (typeflags & SPTYP_ICE) )
    {
        // Must not be wielding an unIDed staff.
        // Note that robes of the Archmagi identify on wearing,
        // so that's less of an issue.
        const item_def* wpn = player_weapon();
        if ( wpn == NULL ||
             wpn->base_type != OBJ_STAVES ||
             item_ident(*wpn, ISFLAG_KNOW_PROPERTIES) )
        {
            int num_unknown = 0;
            for ( int i = EQ_LEFT_RING; i <= EQ_RIGHT_RING; ++i )
            {
                if (you.equip[i] != -1 &&
                    !item_ident(you.inv[you.equip[i]], ISFLAG_KNOW_PROPERTIES))
                {
                    ++num_unknown;
                }
            }

            // We can also identify cases with two unknown rings, both
            // of fire (or both of ice)...let's skip it.
            if ( num_unknown == 1 )
            {
                for ( int i = EQ_LEFT_RING; i <= EQ_RIGHT_RING; ++i )
                    if ( you.equip[i] != -1 )
                    {
                        item_def& ring = you.inv[you.equip[i]];
                        if (!item_ident(ring, ISFLAG_KNOW_PROPERTIES)
                            && (ring.sub_type == RING_FIRE
                                || ring.sub_type == RING_ICE))
                        {
                            set_ident_type( ring.base_type, ring.sub_type,
                                            ID_KNOWN_TYPE );
                            set_ident_flags(ring, ISFLAG_KNOW_PROPERTIES);
                            mprf("You are wearing: %s",
                                 ring.name(DESC_INVENTORY_EQUIP).c_str());
                        }
                    }

                return true;
            }
        }
    }
    return false;
}

static void _surge_power(spell_type spell)
{
    int enhanced = 0;

    _surge_identify_boosters(spell);

    //jmf: simplified
    enhanced += spell_enhancement(get_spell_disciplines(spell));

    if (enhanced)               // one way or the other {dlb}
    {
        mprf("You feel a%s %s",
             (enhanced < -2)  ? "n extraordinarily" :
             (enhanced == -2) ? "n extremely" :
             (enhanced == 2)  ? " strong" :
             (enhanced > 2)   ? " huge"
                              : "",
             (enhanced < 0) ? "numb sensation."
                            : "surge of power!");
    }
}                               // end surge_power()

static std::string _spell_base_description(spell_type spell)
{
    std::ostringstream desc;

    desc << std::left;

    // spell name
    desc << std::setw(30) << spell_title(spell);

    // spell schools
    bool already = false;
    for ( int i = 0; i <= SPTYP_LAST_EXPONENT; ++i)
    {
        if (spell_typematch(spell, (1<<i)))
        {
            if (already)
                desc << '/';
            desc << spelltype_name(1 << i);
            already = true;
        }
    }

    const int so_far = desc.str().length();
    if ( so_far < 60 )
        desc << std::string(60 - so_far, ' ');

    // spell fail rate, level
    desc << std::setw(12) << failure_rate_to_string(spell_fail(spell))
         << spell_difficulty(spell);

    return desc.str();
}

static std::string _spell_extra_description(spell_type spell)
{
    std::ostringstream desc;

    desc << std::left;

    // spell name
    desc << std::setw(30) << spell_title(spell);

    // spell power, hunger level, level
    desc << std::setw(30) << spell_power_string(spell)
         << std::setw(12) << spell_hunger_string(spell)
         << spell_difficulty(spell);

    return desc.str();
}

int list_spells()
{
    ToggleableMenu spell_menu(MF_SINGLESELECT | MF_ANYPRINTABLE |
        MF_ALWAYS_SHOW_MORE | MF_ALLOW_FORMATTING);
    spell_menu.set_title(
        new ToggleableMenuEntry(
            " Your Spells                      Type            "
            "              Success   Level",
            " Your Spells                      Power           "
            "              Hunger    Level",
            MEL_TITLE));
    spell_menu.set_highlighter(NULL);
    spell_menu.set_more(formatted_string("Press '!' to toggle spell view."));
    spell_menu.add_toggle_key('!');
    spell_menu.set_tag("spell");

    for ( int i = 0; i < 52; ++i )
    {
        const char letter = index_to_letter(i);
        const spell_type spell = get_spell_by_letter(letter);
        if (spell != SPELL_NO_SPELL)
        {
            ToggleableMenuEntry* me =
                new ToggleableMenuEntry(_spell_base_description(spell),
                                        _spell_extra_description(spell),
                                        MEL_ITEM, 1, letter);
            spell_menu.add_entry(me);
        }
    }

    std::vector<MenuEntry*> sel = spell_menu.show();
    redraw_screen();
    if ( sel.empty() )
    {
        return 0;
    }
    else
    {
        ASSERT(sel.size() == 1);
        ASSERT(sel[0]->hotkeys.size() == 1);
        return sel[0]->hotkeys[0];
    }
}

static int _apply_spellcasting_success_boosts(spell_type spell, int chance)
{
    int wizardry = player_mag_abil(false);
    int fail_reduce = 100;
    int wiz_factor = 87;

    if (you.religion == GOD_VEHUMET
        && !player_under_penance() && you.piety >= 50
        && (spell_typematch(spell, SPTYP_CONJURATION)
            || spell_typematch(spell, SPTYP_SUMMONING)))
    {
        // [dshaligram] Fail rate multiplier used to be .5, scaled
        // back to 67%.
        fail_reduce = fail_reduce * 67 / 100;
    }

    // [dshaligram] Apply wizardry factor here, rather than mixed into the
    // pre-scaling spell power.
    while (wizardry-- > 0)
    {
        fail_reduce  = fail_reduce * wiz_factor / 100;
        wiz_factor  += (100 - wiz_factor) / 3;
    }

    // Draconians get a boost to dragon-form.
    if (spell == SPELL_DRAGON_FORM && player_genus(GENPC_DRACONIAN))
        fail_reduce = fail_reduce * 70 / 100;

    // Hard cap on fail rate reduction.
    if (fail_reduce < 50)
        fail_reduce = 50;

    return (chance * fail_reduce / 100);
}

int spell_fail(spell_type spell)
{
    int chance = 60;
    int chance2 = 0, armour = 0;

    chance -= 6 * calc_spell_power( spell, false, true );
    chance -= (you.intel * 2);

    //chance -= (you.intel - 10) * abs(you.intel - 10);
    //chance += spell_difficulty(spell) * spell_difficulty(spell) * 3; //spell_difficulty(spell);

    if (you.equip[EQ_BODY_ARMOUR] != -1)
    {

        int ev_penalty = abs(property( you.inv[you.equip[EQ_BODY_ARMOUR]],
                                       PARM_EVASION ));

        // The minus 15 is to make the -1 light armours not so bad
        armour += (20 * ev_penalty) - 15;

        //jmf: armour skill now reduces failure due to armour
        //bwr: this was far too good, an armour skill of 5 was
        //     completely negating plate mail.  Plate mail should
        //     hardly be completely negated, it should still be
        //     an important consideration for even high level characters.
        //     Truth is, even a much worse penalty than the above can
        //     easily be overcome by gaining spell skills... and a lot
        //     faster than any reasonable rate of bonus here.
        int lim_str = (you.strength > 30) ? 30 :
                      (you.strength < 10) ? 10 : you.strength;

        armour -= ((you.skills[SK_ARMOUR] * lim_str) / 15);

        int race_arm = get_equip_race( you.inv[you.equip[EQ_BODY_ARMOUR]] );
        int racial_type = 0;

        if (player_genus(GENPC_DWARVEN))
            racial_type = ISFLAG_DWARVEN;
        else if (player_genus(GENPC_ELVEN))
            racial_type = ISFLAG_ELVEN;
        else if (you.species == SP_HILL_ORC)
            racial_type = ISFLAG_ORCISH;

        // Elven armour gives everyone some benefit to spellcasting,
        // Dwarven armour hinders everyone.
        switch (race_arm)
        {
        case ISFLAG_ELVEN:
            armour -= 20;
            break;
        case ISFLAG_DWARVEN:
            armour += 10;
            break;
        default:
            break;
        }

        // Armour of the same racial type reduces penalty.
        if (racial_type && race_arm == racial_type)
            armour -= 10;

        if (armour > 0)
            chance += armour;
    }

    if (you.equip[EQ_WEAPON] != -1
        && you.inv[you.equip[EQ_WEAPON]].base_type == OBJ_WEAPONS)
    {
        int wpn_penalty = (3 * (property( you.inv[you.equip[EQ_WEAPON]], PWPN_SPEED ) - 12)) / 2;

        if (wpn_penalty > 0)
            chance += wpn_penalty;
    }

    if (you.equip[EQ_SHIELD] != -1)
    {
        switch (you.inv[you.equip[EQ_SHIELD]].sub_type)
        {
        case ARM_BUCKLER:
            chance += 5;
            break;

        case ARM_SHIELD:
            chance += 15;
            break;

        case ARM_LARGE_SHIELD:
            // *BCR* Large chars now get a lower penalty for large shields
            if ((you.species >= SP_OGRE && you.species <= SP_OGRE_MAGE)
                || player_genus(GENPC_DRACONIAN))
            {
                chance += 20;
            }
            else
                chance += 30;
            break;
        }
    }

    switch (spell_difficulty(spell))
    {
    case  1: chance +=   3; break;
    case  2: chance +=  15; break;
    case  3: chance +=  35; break;
    case  4: chance +=  70; break;
    case  5: chance += 100; break;
    case  6: chance += 150; break;
    case  7: chance += 200; break;
    case  8: chance += 260; break;
    case  9: chance += 330; break;
    case 10: chance += 420; break;
    case 11: chance += 500; break;
    case 12: chance += 600; break;
    default: chance += 750; break;
    }

    chance2 = chance;

    if (chance < 45)
        chance2 = 45;
    if (chance < 42)
        chance2 = 43;
    if (chance < 38)
        chance2 = 41;
    if (chance < 35)
        chance2 = 40;
    if (chance < 32)
        chance2 = 38;
    if (chance < 28)
        chance2 = 36;
    if (chance < 22)
        chance2 = 34;
    if (chance < 16)
        chance2 = 32;
    if (chance < 10)
        chance2 = 30;
    if (chance < 2)
        chance2 = 28;
    if (chance < -7)
        chance2 = 26;
    if (chance < -12)
        chance2 = 24;
    if (chance < -18)
        chance2 = 22;
    if (chance < -24)
        chance2 = 20;
    if (chance < -30)
        chance2 = 18;
    if (chance < -38)
        chance2 = 16;
    if (chance < -46)
        chance2 = 14;
    if (chance < -60)
        chance2 = 12;
    if (chance < -80)
        chance2 = 10;
    if (chance < -100)
        chance2 = 8;
    if (chance < -120)
        chance2 = 6;
    if (chance < -140)
        chance2 = 4;
    if (chance < -160)
        chance2 = 2;
    if (chance < -180)
        chance2 = 0;

    if (you.duration[DUR_TRANSFORMATION] > 0)
    {
        switch (you.attribute[ATTR_TRANSFORMATION])
        {
        case TRAN_BLADE_HANDS:
            chance2 += 20;
            break;

        case TRAN_SPIDER:
        case TRAN_BAT:
            chance2 += 10;
            break;
        }
    }

    // Apply the effects of Vehumet and items of wizardry.
    chance2 = _apply_spellcasting_success_boosts(spell, chance2);

    if (chance2 > 100)
        chance2 = 100;

    return (chance2);
}                               // end spell_fail()


int calc_spell_power(spell_type spell, bool apply_intel, bool fail_rate_check)
{
    unsigned int bit;
    int ndx;

    // When checking failure rates, wizardry is handled after the various
    // stepping calulations.
    int power = (you.skills[SK_SPELLCASTING] / 2)
                 + (fail_rate_check? 0 : player_mag_abil(false));
    int enhanced = 0;

    unsigned int disciplines = get_spell_disciplines( spell );

    //jmf: evil evil evil -- exclude HOLY bit
    disciplines &= (~SPTYP_HOLY);

    int skillcount = count_bits( disciplines );
    if (skillcount)
    {
        for (ndx = 0; ndx <= SPTYP_LAST_EXPONENT; ndx++)
        {
            bit = 1 << ndx;
            if ((bit != SPTYP_HOLY) && (disciplines & bit))
            {
                int skill = spell_type2skill( bit );

                power += (you.skills[skill] * 2) / skillcount;
            }
        }
    }

    if (apply_intel)
        power = (power * you.intel) / 10;

    // [dshaligram] Enhancers don't affect fail rates any more, only spell
    // power. Note that this does not affect Vehumet's boost in castability.
    if (!fail_rate_check)
        enhanced = spell_enhancement( disciplines );

    if (enhanced > 0)
    {
        for (ndx = 0; ndx < enhanced; ndx++)
        {
            power *= 15;
            power /= 10;
        }
    }
    else if (enhanced < 0)
    {
        for (ndx = enhanced; ndx < 0; ndx++)
            power /= 2;
    }

    power = stepdown_value( power, 50, 50, 150, 200 );

    return (power);
}                               // end calc_spell_power()


int spell_enhancement( unsigned int typeflags )
{
    int enhanced = 0;

    if (typeflags & SPTYP_CONJURATION)
        enhanced += player_spec_conj();

    if (typeflags & SPTYP_ENCHANTMENT)
        enhanced += player_spec_ench();

    if (typeflags & SPTYP_SUMMONING)
        enhanced += player_spec_summ();

    if (typeflags & SPTYP_POISON)
        enhanced += player_spec_poison();

    if (typeflags & SPTYP_NECROMANCY)
        enhanced += player_spec_death() - player_spec_holy();

    if (typeflags & SPTYP_FIRE)
        enhanced += player_spec_fire() - player_spec_cold();

    if (typeflags & SPTYP_ICE)
        enhanced += player_spec_cold() - player_spec_fire();

    if (typeflags & SPTYP_EARTH)
        enhanced += player_spec_earth() - player_spec_air();

    if (typeflags & SPTYP_AIR)
        enhanced += player_spec_air() - player_spec_earth();

    if (you.special_wield == SPWLD_SHADOW)
        enhanced -= 2;

    // These are used in an exponential way, so we'll limit them a bit. -- bwr
    if (enhanced > 3)
        enhanced = 3;
    else if (enhanced < -3)
        enhanced = -3;

    return (enhanced);
}                               // end spell_enhancement()

void inspect_spells()
{
    if (!you.spell_no)
    {
        mpr("You don't know any spells.");
        return;
    }

    // Maybe we should honour auto_list here, but if you want the
    // description, you probably want the listing, too.
    int keyin = list_spells();
    if ( isalpha(keyin) )
    {
        describe_spell(get_spell_by_letter(keyin));
        redraw_screen();
    }
}

// returns false if spell failed, and true otherwise
bool cast_a_spell()
{
    if (!you.spell_no)
    {
        mpr("You don't know any spells.");
        crawl_state.zero_turns_taken();
        return (false);
    }

    if (you.duration[DUR_BERSERKER])
    {
        canned_msg(MSG_TOO_BERSERK);
        return (false);
    }

    if (silenced(you.x_pos, you.y_pos))
    {
        mpr("You cannot cast spells when silenced!");
        crawl_state.zero_turns_taken();
        more();
        return (false);
    }

    int keyin = 0;              // silence stupid compilers

    while (true)
    {
        mpr( "Cast which spell ([?*] list)? ", MSGCH_PROMPT );

        keyin = get_ch();

        if (keyin == '?' || keyin == '*')
        {
            keyin = list_spells();
            if (!keyin)
                keyin = ESCAPE;

            redraw_screen();

            if ( isalpha(keyin) || keyin == ESCAPE )
                break;
            else
                mesclr();
        }
        else
        {
            break;
        }
    }

    if (keyin == ESCAPE)
    {
        canned_msg( MSG_OK );
        return (false);
    }

    if (!isalpha(keyin))
    {
        mpr("You don't know that spell.");
        crawl_state.zero_turns_taken();
        return (false);
    }

    const spell_type spell = get_spell_by_letter( keyin );

    if (spell == SPELL_NO_SPELL)
    {
        mpr("You don't know that spell.");
        crawl_state.zero_turns_taken();
        return (false);
    }

    if (spell_mana( spell ) > you.magic_points)
    {
        mpr("You don't have enough magic to cast that spell.");
        return (false);
    }

    if (you.is_undead != US_UNDEAD && you.species != SP_VAMPIRE
        && (you.hunger_state == HS_STARVING
            || you.hunger <= spell_hunger( spell )))
    {
        mpr("You don't have the energy to cast that spell.");
        return (false);
    }

    const bool staff_energy = player_energy();
    if (you.duration[DUR_CONF])
        random_uselessness();
    else
    {
        const spret_type cast_result = your_spells( spell );
        if (cast_result == SPRET_ABORT)
        {
            crawl_state.zero_turns_taken();
            return (false);
        }

        exercise_spell( spell, true, cast_result == SPRET_SUCCESS );
        did_god_conduct( DID_SPELL_CASTING, 1 + random2(5) );
    }

    dec_mp( spell_mana(spell) );

    if (!staff_energy && you.is_undead != US_UNDEAD)
    {
        const int spellh = calc_hunger( spell_hunger(spell) );
        if (spellh > 0)
        {
            make_hungry(spellh, true);
            learned_something_new(TUT_SPELL_HUNGER);
        }
    }

    you.turn_is_over = true;
    alert_nearby_monsters();

    return (true);
}                               // end cast_a_spell()

// "Utility" spells for the sake of simplicity are currently ones with
// enchantments, translocations, or divinations.
static bool _spell_is_utility_spell( spell_type spell_id )
{
    return (spell_typematch( spell_id,
                SPTYP_ENCHANTMENT | SPTYP_TRANSLOCATION | SPTYP_DIVINATION ));
}

static bool _spell_is_unholy( spell_type spell_id )
{
    return (testbits( get_spell_flags( spell_id ), SPFLAG_UNHOLY ));
}

bool maybe_identify_staff(item_def &item, spell_type spell)
{
    if (item_type_known(item))
        return (true);

    int relevant_skill = 0;
    const bool chance = (spell != SPELL_NO_SPELL);

    switch (item.sub_type)
    {
        case STAFF_ENERGY:
            if (!chance) // The staff of energy only autoIDs by chance.
                return (false);
            // intentional fall-through
        case STAFF_WIZARDRY:
            relevant_skill = you.skills[SK_SPELLCASTING];
            break;

        case STAFF_FIRE:
            if (!chance || spell_typematch(spell, SPTYP_FIRE))
                relevant_skill = you.skills[SK_FIRE_MAGIC];
            else if (spell_typematch(spell, SPTYP_ICE))
                relevant_skill = you.skills[SK_ICE_MAGIC];
            break;

        case STAFF_COLD:
            if (!chance || spell_typematch(spell, SPTYP_ICE))
                relevant_skill = you.skills[SK_ICE_MAGIC];
            else if (spell_typematch(spell, SPTYP_FIRE))
                relevant_skill = you.skills[SK_FIRE_MAGIC];
            break;

        case STAFF_AIR:
            if (!chance || spell_typematch(spell, SPTYP_AIR))
                relevant_skill = you.skills[SK_AIR_MAGIC];
            else if (spell_typematch(spell, SPTYP_EARTH))
                relevant_skill = you.skills[SK_EARTH_MAGIC];
            break;

        case STAFF_EARTH:
            if (!chance || spell_typematch(spell, SPTYP_EARTH))
                relevant_skill = you.skills[SK_EARTH_MAGIC];
            else if (spell_typematch(spell, SPTYP_AIR))
                relevant_skill = you.skills[SK_AIR_MAGIC];
            break;

        case STAFF_POISON:
            if (!chance || spell_typematch(spell, SPTYP_POISON))
                relevant_skill = you.skills[SK_POISON_MAGIC];
            break;

        case STAFF_DEATH:
            if (!chance || spell_typematch(spell, SPTYP_NECROMANCY))
                relevant_skill = you.skills[SK_NECROMANCY];
            break;

        case STAFF_CONJURATION:
            if (!chance || spell_typematch(spell, SPTYP_CONJURATION))
                relevant_skill = you.skills[SK_CONJURATIONS];
            break;

        case STAFF_ENCHANTMENT:
            if (!chance || spell_typematch(spell, SPTYP_ENCHANTMENT))
                relevant_skill = you.skills[SK_ENCHANTMENTS];
            break;

        case STAFF_SUMMONING:
            if (!chance || spell_typematch(spell, SPTYP_SUMMONING))
                relevant_skill = you.skills[SK_SUMMONINGS];
            break;
    }

    bool id_staff = false;

    if (chance)
    {
        if (you.skills[SK_SPELLCASTING] > relevant_skill)
            relevant_skill = you.skills[SK_SPELLCASTING];

        if (random2(100) < relevant_skill)
            id_staff = true;
    }
    else if (relevant_skill >= 4)
        id_staff = true;

    if (id_staff)
    {
        item_def& wpn = you.inv[you.equip[EQ_WEAPON]];
        // changed from ISFLAG_KNOW_TYPE
        set_ident_flags( wpn, ISFLAG_IDENT_MASK);
        mprf("You are wielding %s.", wpn.name(DESC_NOCAP_A).c_str());
        more();

        you.wield_change = true;
    }
    return (id_staff);
}

static void _spellcasting_side_effects(spell_type spell, bool idonly = false)
{
    if (you.equip[EQ_WEAPON] != -1
        && item_is_staff( you.inv[you.equip[EQ_WEAPON]] ))
    {
        maybe_identify_staff(you.inv[you.equip[EQ_WEAPON]], spell);
    }

    if (idonly)
        return;

    if (!_spell_is_utility_spell(spell))
        did_god_conduct( DID_SPELL_NONUTILITY, 10 + spell_difficulty(spell) );

    // Self-banishment gets a special exemption - you're there to spread light
    if (_spell_is_unholy(spell) && !you.banished)
        did_god_conduct( DID_UNHOLY, 10 + spell_difficulty(spell) );

    // Linley says: Condensation Shield needs some disadvantages to keep
    // it from being a no-brainer... this isn't much, but its a start -- bwr
    if (spell_typematch(spell, SPTYP_FIRE))
        expose_player_to_element(BEAM_FIRE, 0);

    if (spell_typematch(spell, SPTYP_NECROMANCY))
    {
        did_god_conduct( DID_NECROMANCY, 10 + spell_difficulty(spell) );

        if (spell == SPELL_NECROMUTATION && is_good_god(you.religion))
            excommunication();
    }

    alert_nearby_monsters();
}

static bool _vampire_cannot_cast(spell_type spell)
{
    if (you.species != SP_VAMPIRE)
        return false;

    if (you.hunger_state > HS_SATIATED)
        return false;

    // Satiated or less
    switch (spell)
    {
    case SPELL_AIR_WALK:
    case SPELL_ALTER_SELF:
    case SPELL_BERSERKER_RAGE:
    case SPELL_BLADE_HANDS:
    case SPELL_CURE_POISON_II:
    case SPELL_DRAGON_FORM:
    case SPELL_ICE_FORM:
    case SPELL_RESIST_POISON:
    case SPELL_SPIDER_FORM:
    case SPELL_STATUE_FORM:
    case SPELL_TAME_BEASTS:
        return true;
    default:
        return false;
    }
}

static bool _spell_is_uncastable(spell_type spell)
{
    if (you.is_undead && spell_typematch(spell, SPTYP_HOLY))
    {
        mpr( "You can't use this type of magic!" );
        return (true);
    }

    // Normally undead can't memorise these spells, so this check is
    // to catch those in Lich form.  As such, we allow the Lich form
    // to be extended here. -- bwr
    if (spell != SPELL_NECROMUTATION
        && undead_cannot_memorise( spell, you.is_undead ))
    {
        mpr( "You cannot cast that spell in your current form!" );
        return (true);
    }

    if (spell == SPELL_SYMBOL_OF_TORMENT && player_res_torment())
    {
        mpr("To torment others, one must first know what torment means. ");
        return (true);
    }

    if (_vampire_cannot_cast( spell ))
    {
        mpr("Your current blood level is not sufficient to cast that spell.");
        return (true);
    }

    return (false);
}

// Returns SPRET_SUCCESS if spell is successfully cast for purposes of
// exercising, SPRET_FAIL otherwise, or SPRET_ABORT if the player canceled
// the casting.
// Not all of these are actually real spells; invocations, decks, rods or misc.
// effects might also land us here.
// Others are currently unused or unimplemented.
spret_type your_spells(spell_type spell, int powc, bool allow_fail)
{
    struct dist spd;
    struct bolt beam;

    // [dshaligram] Any action that depends on the spellcasting attempt to have
    // succeeded must be performed after the switch().

    if (_spell_is_uncastable(spell))
        return (SPRET_ABORT);

    const int flags = get_spell_flags(spell);

    int potion = -1;

    // XXX: This handles only some of the cases where spells need targeting...
    // there are others that do their own that will be missed by this
    // (and thus will not properly ESC without cost because of it).
    // Hopefully, those will eventually be fixed. -- bwr
    if ((flags & SPFLAG_TARGETING_MASK) && spell != SPELL_PORTAL_PROJECTILE)
    {
        targ_mode_type targ =
            (testbits(flags, SPFLAG_HELPFUL) ? TARG_FRIEND : TARG_ENEMY);

        targeting_type dir  =
            (testbits( flags, SPFLAG_TARGET ) ? DIR_TARGET :
             testbits( flags, SPFLAG_GRID )   ? DIR_TARGET :
             testbits( flags, SPFLAG_DIR )    ? DIR_DIR
                                              : DIR_NONE);

        const char *prompt = get_spell_target_prompt(spell);
        if (spell == SPELL_EVAPORATE)
        {
            potion = prompt_invent_item( "Throw which potion?",
                                         MT_INVLIST, OBJ_POTIONS );
            if (potion == -1)
                return (SPRET_ABORT);
            else if (you.inv[potion].base_type != OBJ_POTIONS)
            {
                mpr( "This spell works only on potions!" );
                return (SPRET_ABORT);
            }
            mprf(MSGCH_PROMPT, "Where do you want to aim %s?",
                               you.inv[potion].name(DESC_NOCAP_YOUR).c_str());
        }
        else if (dir == DIR_DIR)
            mpr(prompt? prompt : "Which direction? ", MSGCH_PROMPT);

        const bool needs_path = (!testbits(flags, SPFLAG_GRID)
                                 && !testbits(flags, SPFLAG_TARGET));

        if (!spell_direction(spd, beam, dir, targ, needs_path, prompt))
            return (SPRET_ABORT);

        if (testbits( flags, SPFLAG_NOT_SELF ) && spd.isMe)
        {
            if (spell == SPELL_TELEPORT_OTHER || spell == SPELL_HEAL_OTHER
                || spell == SPELL_POLYMORPH_OTHER || spell == SPELL_BANISHMENT)
            {
                mpr( "Sorry, this spell works on others only." );
            }
            else
                canned_msg(MSG_UNTHINKING_ACT);

            return (SPRET_ABORT);
        }
    }

    // Enhancers only matter for calc_spell_power() and spell_fail().
    // Not sure about this: is it flavour or misleading? (jpeg)
    if (powc == 0 || allow_fail)
        _surge_power(spell);

    // Added this so that the passed in powc can have meaning -- bwr
    // Remember that most holy spells don't yet use powc!
    if (powc == 0)
        powc = calc_spell_power( spell, true );

    if (allow_fail)
    {
        int spfl = random2avg(100, 3);

        if (you.religion != GOD_SIF_MUNA
            && you.penance[GOD_SIF_MUNA] && one_chance_in(20))
        {
            god_speaks(GOD_SIF_MUNA, "You feel a surge of divine spite.");

            // This will cause failure and increase the miscast effect.
            spfl = -you.penance[GOD_SIF_MUNA];

            // Reduced penance reduction here because casting spells
            // is a player controllable act.  -- bwr
            if (one_chance_in(12))
                dec_penance(GOD_SIF_MUNA, 1);
        }

        const int spfail_chance = spell_fail(spell);
        // Divination mappings backfire in Labyrinths.
        if (you.level_type == LEVEL_LABYRINTH
            && testbits(flags, SPFLAG_MAPPING))
        {
            mprf(MSGCH_WARN,
                 "The warped magic of this place twists your "
                 "spell in on itself!");
            spfl = spfail_chance / 2 - 1;
        }

        if (spfl < spfail_chance)
        {
            _spellcasting_side_effects(spell, true);

            mpr( "You miscast the spell." );
            flush_input_buffer( FLUSH_ON_FAILURE );
            learned_something_new( TUT_SPELL_MISCAST );

            if (you.religion == GOD_SIF_MUNA
                && !player_under_penance()
                && you.piety >= 100 && random2(150) <= you.piety)
            {
                canned_msg(MSG_NOTHING_HAPPENS);
                return SPRET_FAIL;
            }

            unsigned int sptype = 0;

            do
            {
                sptype = 1 << (random2(SPTYP_LAST_EXPONENT+1));
            }
            while (!spell_typematch(spell, sptype));

            // all spell failures give a bit of magical radiation..
            // failure is a function of power squared multiplied
            // by how badly you missed the spell.  High power
            // spells can be quite nasty: 9 * 9 * 90 / 500 = 15
            // points of contamination!
            int nastiness = spell_mana(spell) * spell_mana(spell)
                                              * (spfail_chance - spfl) + 250;

            const int cont_points = div_rand_round(nastiness, 500);

            // miscasts are uncontrolled
            contaminate_player( cont_points );

            miscast_effect( sptype, spell_mana(spell),
                            spfail_chance - spfl, 100 );

            return (SPRET_FAIL);
        }
    }

#if DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "Spell #%d, power=%d", spell, powc );
#endif

    const bool god_gift = crawl_state.is_god_acting();

    switch (spell)
    {
    // Attack spells.
    // using burn_freeze()
    case SPELL_BURN:
        if (burn_freeze(powc, BEAM_FIRE) == -1)
            return (SPRET_ABORT);
        break;

    case SPELL_FREEZE:
        if (burn_freeze(powc, BEAM_COLD) == -1)
            return (SPRET_ABORT);
        break;

    case SPELL_CRUSH:
        if (burn_freeze(powc, BEAM_MISSILE) == -1)
            return (SPRET_ABORT);
        break;

    case SPELL_ARC:
        if (burn_freeze(powc, BEAM_ELECTRICITY) == -1)
            return (SPRET_ABORT);
        break;

    // direct beams/bolts
    case SPELL_MAGIC_DART:
        if (!zapping(ZAP_MAGIC_DARTS, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_STRIKING:
        if (!zapping(ZAP_STRIKING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_THROW_FLAME:
        if (!zapping(ZAP_FLAME, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_THROW_FROST:
        if (!zapping(ZAP_FROST, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_PAIN:
        if (!zapping(ZAP_PAIN, powc, beam, true))
            return (SPRET_ABORT);
        dec_hp(1, false);
        break;

    case SPELL_FLAME_TONGUE:
        if (!zapping(ZAP_FLAME_TONGUE, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_SANDBLAST:
        if (!cast_sandblast(powc, beam))
            return (SPRET_ABORT);
        break;

    case SPELL_BONE_SHARDS:
        if (!cast_bone_shards(powc, beam))
            return (SPRET_ABORT);
        break;

    case SPELL_SHOCK:
        if (!zapping(ZAP_ELECTRICITY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_STING:
        if (!zapping(ZAP_STING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_VAMPIRIC_DRAINING:
        vampiric_drain(powc, spd);
        break;

    case SPELL_BOLT_OF_FIRE:
        if (!zapping(ZAP_FIRE, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_BOLT_OF_COLD:
        if (!zapping(ZAP_COLD, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_STONE_ARROW:
        if (!zapping(ZAP_STONE_ARROW, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_POISON_ARROW:
        if (!zapping(ZAP_POISON_ARROW, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_BOLT_OF_IRON:
        if (!zapping(ZAP_IRON_BOLT, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_LIGHTNING_BOLT:
        if (!zapping(ZAP_LIGHTNING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_BOLT_OF_MAGMA:
        if (!zapping(ZAP_MAGMA, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_VENOM_BOLT:
        if (!zapping(ZAP_VENOM_BOLT, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_BOLT_OF_DRAINING:
        if (!zapping(ZAP_NEGATIVE_ENERGY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_LEHUDIBS_CRYSTAL_SPEAR:
        if (!zapping(ZAP_CRYSTAL_SPEAR, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_BOLT_OF_INACCURACY:
        if (!zapping(ZAP_BEAM_OF_ENERGY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_STICKY_FLAME:
        if (!zapping(ZAP_STICKY_FLAME, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_DISPEL_UNDEAD:
        if (!zapping(ZAP_DISPEL_UNDEAD, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_ISKENDERUNS_MYSTIC_BLAST:
        if (!zapping(ZAP_MYSTIC_BLAST, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_THUNDERBOLT:
        if (!zapping(ZAP_LIGHTNING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_AGONY:
        if (!zapping(ZAP_AGONY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_DISRUPT:
        if (!zapping(ZAP_DISRUPTION, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_DISINTEGRATE:
        if (!zapping(ZAP_DISINTEGRATION, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_ICE_BOLT:
        if (!zapping(ZAP_ICE_BOLT, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_ORB_OF_FRAGMENTATION:
        if (!zapping(ZAP_ORB_OF_FRAGMENTATION, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_CIGOTUVIS_DEGENERATION:
        if (!zapping(ZAP_DEGENERATION, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_ORB_OF_ELECTROCUTION:
        if (!zapping(ZAP_ORB_OF_ELECTRICITY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_FLAME_OF_CLEANSING:
        if (!zapping(ZAP_CLEANSING_FLAME, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_HOLY_WORD:
        holy_word(50, HOLY_WORD_SPELL, you.x_pos, you.y_pos, true);
        break;

    case SPELL_REPEL_UNDEAD:
        turn_undead(50);
        break;

    case SPELL_HELLFIRE:
        // Should only be available from
        // staff of Dispater & Sceptre of Asmodeus
        if (!zapping(ZAP_HELLFIRE, powc, beam, true))
            return (SPRET_ABORT);
        break;

    // Clouds and explosions.
    case SPELL_MEPHITIC_CLOUD:
        if (!stinking_cloud(powc, beam))
            return (SPRET_ABORT);
        break;

    case SPELL_EVAPORATE:
        if (!cast_evaporate(powc, beam, potion))
            return SPRET_ABORT;
        break;

    case SPELL_POISONOUS_CLOUD:
        cast_big_c(powc, CLOUD_POISON, KC_YOU, beam);
        break;

    case SPELL_FREEZING_CLOUD:
        cast_big_c(powc, CLOUD_COLD, KC_YOU, beam);
        break;

    case SPELL_FIRE_STORM:
        cast_fire_storm(powc, beam);
        break;

    case SPELL_ICE_STORM:
        if (!zapping(ZAP_ICE_STORM, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_FIREBALL:
        if (!fireball(powc, beam))
            return (SPRET_ABORT);
        break;

    case SPELL_DELAYED_FIREBALL:
        crawl_state.cant_cmd_repeat("You can't repeat delayed fireball.");
        // This spell has two main advantages over Fireball:
        //
        // (1) The release is instantaneous, so monsters will not
        //     get an action before the player... this allows the
        //     player to hit monsters with a double fireball (this
        //     is why we only allow one delayed fireball at a time,
        //     if you want to allow for more, then the release should
        //     take at least some amount of time).
        //
        //     The casting of this spell still costs a turn.  So
        //     casting Delayed Fireball and immediately releasing
        //     the fireball is only slightly different than casting
        //     a regular Fireball (monsters act in the middle instead
        //     of at the end).  This is why we allow for the spell
        //     level discount so that Fireball is free with this spell
        //     (so that it only costs 7 levels instead of 13 to have
        //     both).
        //
        // (2) When the fireball is released, it is guaranteed to
        //     go off... the spell only fails at this point.  This can
        //     be a large advantage for characters who have difficulty
        //     casting Fireball in their standard equipment.  However,
        //     the power level for the actual fireball is determined at
        //     release, so if you do swap out your enhancers you'll
        //     get a less powerful ball when its released. -- bwr
        //
        if (!you.attribute[ ATTR_DELAYED_FIREBALL ])
        {
            // okay, this message is weak but functional -- bwr
            mpr( "You feel magically charged." );
            you.attribute[ ATTR_DELAYED_FIREBALL ] = 1;
        }
        else
            canned_msg( MSG_NOTHING_HAPPENS );
        break;

    // LOS spells
    case SPELL_SMITING:
        cast_smiting(powc, spd);
        break;

    case SPELL_TWIST:
        cast_twist(powc);
        break;

    case SPELL_AIRSTRIKE:
        airstrike(powc, spd);
        break;

    case SPELL_FRAGMENTATION:
        cast_fragmentation(powc);
        break;

    case SPELL_FAR_STRIKE:
        cast_far_strike(powc);
        break;

    case SPELL_PORTAL_PROJECTILE:
        if (!cast_portal_projectile(powc))
            return SPRET_ABORT;
        break;

    // other effects
    case SPELL_DISCHARGE:
        cast_discharge(powc);
        break;

    case SPELL_CHAIN_LIGHTNING:
        cast_chain_lightning(powc);
        break;

    case SPELL_DISPERSAL:
        cast_dispersal(powc);
        break;

    case SPELL_SHATTER:
        cast_shatter(powc);
        break;

    case SPELL_BEND:
        cast_bend(powc);
        break;

    case SPELL_SYMBOL_OF_TORMENT:
        torment(TORMENT_SPELL, you.x_pos, you.y_pos);
        break;

    case SPELL_OZOCUBUS_REFRIGERATION:
        cast_refrigeration(powc);
        break;

    case SPELL_IGNITE_POISON:
        cast_ignite_poison(powc);
        break;

    case SPELL_ROTTING:
        cast_rotting(powc);
        break;

    // Summoning spells, and other spells that create new monsters.
    // If a god is making you cast one of these spells, any monsters
    // produced will count as god gifts.
    case SPELL_SUMMON_BUTTERFLIES:
        cast_summon_butterflies(powc, god_gift);
        break;

    case SPELL_SUMMON_SMALL_MAMMALS:
        cast_summon_small_mammals(powc, god_gift);
        break;

    case SPELL_STICKS_TO_SNAKES:
        cast_sticks_to_snakes(powc, god_gift);
        break;

    case SPELL_SUMMON_SCORPIONS:
        cast_summon_scorpions(powc, god_gift);
        break;

    case SPELL_SUMMON_SWARM:
        cast_summon_swarm(powc, god_gift);
        break;

    case SPELL_CALL_CANINE_FAMILIAR:
        cast_call_canine_familiar(powc, god_gift);
        break;

    case SPELL_SUMMON_ELEMENTAL:
        if (!summon_elemental(powc, god_gift))
            return (SPRET_ABORT);
        break;

    case SPELL_SUMMON_ICE_BEAST:
        cast_summon_ice_beast(powc, god_gift);
        break;

    case SPELL_SUMMON_UGLY_THING:
        cast_summon_ugly_thing(powc, god_gift);
        break;

    case SPELL_SUMMON_DRAGON:
        cast_summon_dragon(powc, god_gift);
        break;

    case SPELL_SUMMON_GUARDIAN:
        summon_guardian(powc, god_gift);
        break;

    case SPELL_SUMMON_DAEVA:
        summon_daeva(powc, god_gift);
        break;

    case SPELL_TUKIMAS_DANCE:
        // Temporarily turn a wielded weapon into a dancing weapon.
        crawl_state.cant_cmd_repeat("You can't repeat Tukima's Dance.");
        cast_tukimas_dance(powc, god_gift);
        break;

    case SPELL_CONJURE_BALL_LIGHTNING:
        cast_conjure_ball_lightning(powc, god_gift);
        break;

    case SPELL_CALL_IMP:
        cast_call_imp(powc, god_gift);
        break;

    case SPELL_SUMMON_DEMON:
        cast_summon_demon(powc, god_gift);
        break;

    case SPELL_DEMONIC_HORDE:
        cast_demonic_horde(powc, god_gift);
        break;

    case SPELL_SUMMON_GREATER_DEMON:
        cast_summon_greater_demon(powc, god_gift);
        break;

    case SPELL_SHADOW_CREATURES:
        cast_shadow_creatures(god_gift);
        break;

    case SPELL_SUMMON_HORRIBLE_THINGS:
        cast_summon_horrible_things(powc, god_gift);
        break;

    case SPELL_ANIMATE_SKELETON:
        mpr("You attempt to give life to the dead...");
        animate_a_corpse(you.x_pos, you.y_pos, CORPSE_SKELETON, BEH_FRIENDLY,
                         you.pet_target, god_gift);
        break;

    case SPELL_ANIMATE_DEAD:
        mpr("You call on the dead to walk for you.");
        animate_dead(&you, powc + 1, BEH_FRIENDLY, you.pet_target, god_gift);
        break;

    case SPELL_SIMULACRUM:
        cast_simulacrum(powc, god_gift);
        break;

    case SPELL_TWISTED_RESURRECTION:
        cast_twisted_resurrection(powc, god_gift);
        break;

    case SPELL_SUMMON_WRAITHS:
        cast_summon_wraiths(powc, god_gift);
        break;

    case SPELL_DEATH_CHANNEL:
        cast_death_channel(powc, god_gift);
        break;

    // Enchantments.
    case SPELL_CONFUSING_TOUCH:
        cast_confusing_touch(powc);
        break;

    case SPELL_BACKLIGHT:
        if (!zapping(ZAP_BACKLIGHT, powc + 10, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_CAUSE_FEAR:
        mass_enchantment(ENCH_FEAR, powc, MHITYOU);
        break;

    case SPELL_SLOW:
        if (!zapping(ZAP_SLOWING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_CONFUSE:
        if (!zapping(ZAP_CONFUSION, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_ENSLAVEMENT:
        if (!zapping(ZAP_ENSLAVEMENT, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_TAME_BEASTS:
        cast_tame_beasts(powc);
        break;

    case SPELL_SLEEP:
    {
        const int sleep_power =
            stepdown_value( powc * 9 / 10, 5, 35, 45, 50 );
#ifdef DEBUG_DIAGNOSTICS
        mprf(MSGCH_DIAGNOSTICS, "Sleep power stepdown: %d -> %d",
             powc, sleep_power);
#endif
        if (!zapping(ZAP_SLEEP, sleep_power, beam, true))
            return (SPRET_ABORT);
        break;
    }

    case SPELL_PARALYSE:
        if (!zapping(ZAP_PARALYSIS, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_POLYMORPH_OTHER:
        // Trying is already enough, even if it fails.
        did_god_conduct(DID_DELIBERATE_MUTATING, 10);

        if (!zapping(ZAP_POLYMORPH_OTHER, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_TELEPORT_OTHER:
        if (!zapping(ZAP_TELEPORTATION, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_INTOXICATE:
        cast_intoxicate(powc);
        break;

    case SPELL_MASS_CONFUSION:
        mass_enchantment(ENCH_CONFUSION, powc, MHITYOU);
        break;

    case SPELL_MASS_SLEEP:
        cast_mass_sleep(powc);
        break;

    case SPELL_CONTROL_UNDEAD:
        mass_enchantment(ENCH_CHARM, powc, MHITYOU);
        break;

    case SPELL_ABJURATION_I:
    case SPELL_ABJURATION_II:
        abjuration(powc);
        break;

    case SPELL_BANISHMENT:
        if (beam.target_x == you.x_pos && beam.target_y == you.y_pos)
        {
            mpr("You cannot banish yourself!");
            break;
        }
        if (!zapping(ZAP_BANISHMENT, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_OLGREBS_TOXIC_RADIANCE:
        cast_toxic_radiance();
        break;

    // beneficial enchantments
    case SPELL_HASTE:
        if (!zapping(ZAP_HASTING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_INVISIBILITY:
        if (!zapping(ZAP_INVISIBILITY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_LESSER_HEALING:
        if (!cast_healing(5))
            return (SPRET_ABORT);
        break;

    case SPELL_GREATER_HEALING:
        if (!cast_healing(25))
            return (SPRET_ABORT);
        break;

    case SPELL_HEAL_OTHER:
        zapping(ZAP_HEALING, powc, beam);
        break;

    // Self-enchantments. (Spells that can only affect the player.)
    // Resistances.
    case SPELL_INSULATION:
        cast_insulation(powc);
        break;

    case SPELL_RESIST_POISON:
        cast_resist_poison(powc);
        break;

    case SPELL_SEE_INVISIBLE:
        cast_see_invisible(powc);
        break;

    case SPELL_CONTROL_TELEPORT:
        cast_teleport_control(powc);
        break;

    // Healing.
    case SPELL_CURE_POISON_I:   // Ely version
    case SPELL_CURE_POISON_II:  // Poison magic version
        // both use same function
        cast_cure_poison(powc);
        break;

    case SPELL_PURIFICATION:
        purification();
        break;

    case SPELL_RESTORE_STRENGTH:
        restore_stat(STAT_STRENGTH, 0, false);
        break;

    case SPELL_RESTORE_INTELLIGENCE:
        restore_stat(STAT_INTELLIGENCE, 0, false);
        break;

    case SPELL_RESTORE_DEXTERITY:
        restore_stat(STAT_DEXTERITY, 0, false);
        break;

    // Weapon brands.
    case SPELL_SURE_BLADE:
        cast_sure_blade(powc);
        break;

    case SPELL_TUKIMAS_VORPAL_BLADE:
        if (!brand_weapon(SPWPN_VORPAL, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_FIRE_BRAND:
        if (!brand_weapon(SPWPN_FLAMING, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_FREEZING_AURA:
        if (!brand_weapon(SPWPN_FREEZING, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_MAXWELLS_SILVER_HAMMER:
        if (!brand_weapon(SPWPN_DUMMY_CRUSHING, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_POISON_WEAPON:
        if (!brand_weapon(SPWPN_VENOM, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_EXCRUCIATING_WOUNDS:
        if (!brand_weapon(SPWPN_PAIN, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_LETHAL_INFUSION:
        if (!brand_weapon(SPWPN_DRAINING, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_WARP_BRAND:
        if (!brand_weapon(SPWPN_DISTORTION, powc))
            canned_msg(MSG_SPELL_FIZZLES);
        break;

    case SPELL_POISON_AMMUNITION:
        cast_poison_ammo();
        break;

    // Transformations.
    case SPELL_BLADE_HANDS:
        transform(powc, TRAN_BLADE_HANDS);
        break;

    case SPELL_SPIDER_FORM:
        transform(powc, TRAN_SPIDER);
        break;

    case SPELL_STATUE_FORM:
        transform(powc, TRAN_STATUE);
        break;

    case SPELL_ICE_FORM:
        transform(powc, TRAN_ICE_BEAST);
        break;

    case SPELL_DRAGON_FORM:
        transform(powc, TRAN_DRAGON);
        break;

    case SPELL_NECROMUTATION:
        transform(powc, TRAN_LICH);
        break;

    case SPELL_AIR_WALK:
        transform(powc, TRAN_AIR);
        break;

    case SPELL_ALTER_SELF:
        // Trying is already enough, even if it fails.
        did_god_conduct(DID_DELIBERATE_MUTATING, 10);

        crawl_state.cant_cmd_repeat("You can't repeat Alter Self.");
        if (!enough_hp( you.hp_max / 2, true ))
        {
            mpr( "Your body is in too poor a condition "
                 "for this spell to function." );

            return (SPRET_FAIL);
        }

        mpr("Your body is suffused with transfigurative energy!");

        set_hp( 1 + random2(you.hp), false );

        if (!mutate(RANDOM_MUTATION, false))
            mpr("Odd... you don't feel any different.");
        break;

    // General enhancement.
    case SPELL_BERSERKER_RAGE:
        cast_berserk();
        break;

    case SPELL_REGENERATION:
        cast_regen(powc);
        break;

    case SPELL_REPEL_MISSILES:
        missile_prot(powc);
        break;

    case SPELL_DEFLECT_MISSILES:
        deflection(powc);
        break;

    case SPELL_SWIFTNESS:
        cast_swiftness(powc);
        break;

    case SPELL_LEVITATION:
        potion_effect( POT_LEVITATION, powc );
        break;

    case SPELL_FLY:
        cast_fly(powc);
        break;

    case SPELL_STONESKIN:
        cast_stoneskin(powc);
        break;

    case SPELL_STONEMAIL:
        stone_scales(powc);
        break;

    case SPELL_CONDENSATION_SHIELD:
        cast_condensation_shield(powc);
        break;

    case SPELL_OZOCUBUS_ARMOUR:
        ice_armour(powc, false);
        break;

    case SPELL_FORESCRY:
        cast_forescry(powc);
        break;

    case SPELL_SILENCE:
        cast_silence(powc);
        break;

    // other
    case SPELL_SELECTIVE_AMNESIA:
        crawl_state.cant_cmd_repeat("You can't repeat selective amnesia.");

        // Sif Muna power calls with true
        if (!cast_selective_amnesia(false))
            return (SPRET_ABORT);
        break;

    case SPELL_EXTENSION:
        extension(powc);
        break;

    case SPELL_BORGNJORS_REVIVIFICATION:
        cast_revivification(powc);
        break;

    case SPELL_SUBLIMATION_OF_BLOOD:
        cast_sublimation_of_blood(powc);
        break;

    case SPELL_DEATHS_DOOR:
        cast_deaths_door(powc);
        break;

    case SPELL_RING_OF_FLAMES:
        cast_ring_of_flames(powc);
        break;

    // Escape spells.
    case SPELL_BLINK:
        random_blink(true);
        break;

    case SPELL_TELEPORT_SELF:
        you_teleport();
        break;

    case SPELL_SEMI_CONTROLLED_BLINK:
        //jmf: powc is ignored
        if (cast_semi_controlled_blink(powc) == -1)
            return (SPRET_ABORT);
        break;

    case SPELL_CONTROLLED_BLINK:
        if (blink(powc, true) == -1)
            return (SPRET_ABORT);
        break;

    // Utility spells.
    case SPELL_DETECT_CURSE:
        detect_curse(false);
        break;

    case SPELL_REMOVE_CURSE:
        remove_curse(false);
        break;

    case SPELL_IDENTIFY:
        identify(powc);
        break;

    case SPELL_DETECT_SECRET_DOORS:
        cast_detect_secret_doors(powc);
        break;

    case SPELL_DETECT_TRAPS:
        mprf("You detect %s", (detect_traps(powc) > 0) ? "traps!"
                                                       : "nothing.");
        break;

    case SPELL_DETECT_ITEMS:
        mprf("You detect %s", (detect_items(powc) > 0) ? "items!"
                                                       : "nothing.");
        break;

    case SPELL_DETECT_CREATURES:
    {
        int known_plants  = count_detected_plants();
        int num_creatures = detect_creatures(powc);

        if (!num_creatures)
            mpr("You detect nothing.");
        else if (num_creatures == known_plants)
            mpr("You detect no further creatures.");
        else
            mpr("You detect creatures!");
        break;
    }

    case SPELL_MAGIC_MAPPING:
        if (you.level_type == LEVEL_PANDEMONIUM)
        {
            mpr("Your Earth magic cannot map Pandemonium.");
        }
        else
        {
            powc = stepdown_value( powc, 10, 10, 40, 45 );
            magic_mapping( 5 + powc, 50 + random2avg( powc * 2, 2 ), false );
        }
        break;

    case SPELL_CREATE_NOISE:  // Unused, the player can shout to do this. - bwr
        noisy(25, you.x_pos, you.y_pos, "You hear a voice calling your name!");
        break;

    case SPELL_PROJECTED_NOISE:
        project_noise();
        break;

    case SPELL_CONJURE_FLAME:
        if (!conjure_flame(powc))
            return (SPRET_ABORT);
        break;

    case SPELL_DIG:
        if (!zapping(ZAP_DIGGING, powc, beam, true))
            return (SPRET_ABORT);
        break;

    case SPELL_PASSWALL:
        cast_passwall(powc);
        break;

    case SPELL_TOMB_OF_DOROKLOHE:
        entomb(powc);
        break;

    case SPELL_APPORTATION:
        if (cast_apportation(powc) == -1)
            return (SPRET_ABORT);
        break;

    case SPELL_SWAP:
        crawl_state.cant_cmd_repeat("You can't swap.");
        cast_swap(powc);
        break;

    case SPELL_RECALL:
        recall(0);
        break;

    case SPELL_PORTAL:
        crawl_state.cant_cmd_repeat("You can't repeat create portal.");
        if (portal() == -1)
            return (SPRET_ABORT);
        break;

    case SPELL_CORPSE_ROT:
        corpse_rot(0);
        break;

    case SPELL_FULSOME_DISTILLATION:
        cast_fulsome_distillation(powc);
        break;

    case SPELL_DETECT_MAGIC:
        mpr("FIXME: implement!");
        break;

    case SPELL_DEBUGGING_RAY:
        if (!zapping(ZAP_DEBUGGING_RAY, powc, beam, true))
            return (SPRET_ABORT);
        break;

    default:
        mpr("Invalid spell!");
        break;
    }                           // end switch

    _spellcasting_side_effects(spell);

    return (SPRET_SUCCESS);
}

void exercise_spell( spell_type spell, bool spc, bool success )
{
    // (!success) reduces skill increase for miscast spells
    int ndx = 0;
    int skill;
    int exer = 0;
    int exer_norm = 0;
    int workout = 0;

    // This is used as a reference level to normalise spell skill training
    // (for Sif Muna piety). Normalised skill training is worked out as:
    // norm = actual_amount_trained * species_aptitude / ref_skill. This was
    // set at 50 in stone_soup 0.1.1 (which is bad).
    const int ref_skill = 80;

    unsigned int disciplines = get_spell_disciplines(spell);

    //jmf: evil evil evil -- exclude HOLY bit
    disciplines &= (~SPTYP_HOLY);

    int skillcount = count_bits( disciplines );

    if (!success)
        skillcount += 4 + random2(10);

    const int diff = spell_difficulty(spell);
    for (ndx = 0; ndx <= SPTYP_LAST_EXPONENT; ndx++)
    {
        if (!spell_typematch( spell, 1 << ndx ))
            continue;

        skill = spell_type2skill( 1 << ndx );
        workout = (random2(1 + diff) / skillcount);

        if (!one_chance_in(5))
            workout++;       // most recently, this was an automatic add {dlb}

        const int exercise_amount = exercise( skill, workout );
        exer      += exercise_amount;
        exer_norm +=
            exercise_amount * species_skills(skill, you.species) / ref_skill;
    }

    /* ******************************************************************
       Other recent formulae for the above:

       * workout = random2(spell_difficulty(spell_ex)
       * (10 + (spell_difficulty(spell_ex) * 2 )) / 10 / spellsy + 1);

       * workout = spell_difficulty(spell_ex)
       * (15 + spell_difficulty(spell_ex)) / 15 / spellsy;

       spellcasting had also been generally exercised at the same time
       ****************************************************************** */

    if (spc)
    {
        const int exercise_amount =
            exercise(SK_SPELLCASTING, one_chance_in(3) ? 1
                            : random2(1 + random2(diff)));
        exer      += exercise_amount;
        exer_norm += exercise_amount *
            species_skills(SK_SPELLCASTING, you.species) / ref_skill;
    }

    if (exer_norm)
        did_god_conduct( DID_SPELL_PRACTISE, exer_norm );
}                               // end exercise_spell()

static bool _send_abyss(const char *cause)
{
    if (you.level_type != LEVEL_ABYSS)
    {
        you.banish(cause? cause : "");
        return (true);
    }
    else
    {
        mpr("The world appears momentarily distorted.");
        return (false);
    }
}

static void _miscast_conjuration(int severity, const char* cause)
{
    bolt beam;
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            msg::stream << "Sparks fly from your " << your_hand(true)
                        << '!' << std::endl;
            break;
        case 1:
            mpr("The air around you crackles with energy!");
            break;
        case 2:
            msg::stream << "Wisps of smoke drift from your "
                        << your_hand(true) << '.' << std::endl;
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("You are momentarily dazzled by a flash of light!");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("Your skin tingles.");
            break;
        case 7:
            mpr("Your skin glows momentarily.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (player_can_smell())
                mpr("You smell something strange.");
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
        }
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            msg::stream << "Smoke pours from your " << your_hand(true)
                        << '!' << std::endl;
            big_cloud( CLOUD_GREY_SMOKE, KC_YOU,
                       you.x_pos, you.y_pos, 20,
                       7 + random2(7) );
            break;
        case 1:
            mpr("A wave of violent energy washes through your body!");
            ouch(6 + random2avg(7, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("Energy rips through your body!");
            ouch(9 + random2avg(17, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            mpr("You are caught in a violent explosion!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 12 );
            beam.flavour = BEAM_MISSILE; // unsure about this
            // BEAM_EXPLOSION instead? {dlb}

            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "explosion";
            beam.colour = random_colour();
            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                beam.aux_source = cause;
            beam.ex_size = 1;
            beam.is_explosion = true;

            explosion(beam);
            break;
        }
        break;

    case 3:         // considerably less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("You are blasted with magical energy!");
            ouch(12 + random2avg(29, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            mpr("There is a sudden explosion of magical energy!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 20 );
            beam.flavour = BEAM_MISSILE; // unsure about this
            // BEAM_EXPLOSION instead? {dlb}
            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "explosion";
            beam.colour = random_colour();
            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                beam.aux_source = cause;
            beam.ex_size = coinflip() ? 1 : 2;
            beam.is_explosion = true;

            explosion(beam);
            break;
        }
    }
}

static void _miscast_enchantment(int severity, const char* cause)
{
    switch (severity)
    {
    case 0:         // harmless messages only
        switch (random2(10))
        {
        case 0:
            msg::stream << "Your " << your_hand(true)
                        << " glow momentarily." << std::endl;
            break;
        case 1:
            mpr("The air around you crackles with energy!");
            break;
        case 2:
            mpr("Multicolored lights dance before your eyes!");
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("Waves of light ripple over your body.");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("Your skin tingles.");
            break;
        case 7:
            mpr("Your skin glows momentarily.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear something strange.", MSGCH_SOUND);
            else if (you.attribute[ATTR_TRANSFORMATION] != TRAN_AIR)
                mpr("Your skull vibrates slightly.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 1:         // slightly annoying
        switch (random2(2))
        {
        case 0:
            potion_effect(POT_LEVITATION, 20);
            break;
        case 1:
            random_uselessness();
            break;
        }
        break;

    case 2:         // much more annoying
        switch (random2(7))
        {
        case 0:
        case 1:
        case 2:
            mpr("You sense a malignant aura.");
            curse_an_item(false);
            break;
        case 3:
        case 4:
        case 5:
            potion_effect(POT_SLOWING, 10);
            break;
        case 6:
            potion_effect(POT_BERSERK_RAGE, 10);
            break;
        }
        break;

    case 3:         // potentially lethal
        switch (random2(4))
        {
        case 0:
            do
            {
                curse_an_item(false);
            }
            while ( !one_chance_in(3) );

            mpr("You sense an overwhelmingly malignant aura!");
            break;
        case 1:
            potion_effect(POT_PARALYSIS, 10);
            break;
        case 2:
            potion_effect(POT_CONFUSION, 10);
            break;
        case 3:
            mpr("You feel saturated with unharnessed energies!");
            you.magic_contamination += random2avg(19,3);
            break;
        }
        break;
    }
}

static void _miscast_translocation(int severity, const char* cause)
{
    const bool god_gift = crawl_state.is_god_acting();
    const unsigned flags = (god_gift) ? MG_GOD_GIFT : 0;

    switch (severity)
    {
    case 0:         // harmless messages only
        switch (random2(10))
        {
        case 0:
            mpr("Space warps around you.");
            break;
        case 1:
            mpr("The air around you crackles with energy!");
            break;
        case 2:
            mpr("You feel a wrenching sensation.");
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("You spin around.");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("Your skin tingles.");
            break;
        case 7:
            mpr("The world appears momentarily distorted!");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            mpr("You feel uncomfortable.");
            break;
        }
        break;

    case 1:         // mostly harmless
        switch (random2(6))
        {
        case 0:
        case 1:
        case 2:
            mpr("You are caught in a localised field of spatial distortion.");
            ouch(4 + random2avg(9, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 3:
        case 4:
            mpr("Space bends around you!");
            random_blink(false);
            ouch(4 + random2avg(7, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 5:
            if (create_monster(
                    mgen_data::alert_hostile_at(MONS_SPATIAL_VORTEX,
                        you.pos(), 3, flags)) != -1)
            {
                mpr("Space twists in upon itself!");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 2:         // less harmless
        switch (random2(7))
        {
        case 0:
        case 1:
        case 2:
            mpr("You are caught in a strong localised spatial distortion.");
            ouch(9 + random2avg(23, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 3:
        case 4:
            mpr("Space warps around you!");

            if (one_chance_in(3))
                you_teleport_now( true );
            else
                random_blink( false );

            ouch(5 + random2avg(9, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            potion_effect(POT_CONFUSION, 40);
            break;
        case 5:
        {
            bool success = false;

            for (int i = 1 + random2(3); i >= 0; --i)
            {
                if (create_monster(
                        mgen_data::alert_hostile_at(MONS_SPATIAL_VORTEX,
                            you.pos(), 3, flags)) != -1)
                {
                    success = true;
                }
            }

            if (success)
                mpr("Space twists in upon itself!");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        case 6:
            _send_abyss(cause);
            break;
        }
        break;

    case 3:         // much less harmless
        switch (random2(4))
        {
        case 0:
            mpr("You are caught in an extremely strong localised spatial distortion!");
            ouch(15 + random2avg(29, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            mpr("Space warps crazily around you!");
            you_teleport_now( true );

            ouch(9 + random2avg(17, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            potion_effect(POT_CONFUSION, 60);
            break;
        case 2:
            _send_abyss(cause);
            break;
        case 3:
            mpr("You feel saturated with unharnessed energies!");
            you.magic_contamination += random2avg(19,3);
            break;
        }
        break;
    }
}

static void _miscast_summoning(int severity, const char* cause)
{
    const bool god_gift = crawl_state.is_god_acting();
    const unsigned flags = (god_gift) ? MG_GOD_GIFT : 0;

    switch (severity)
    {
    case 0:         // harmless messages only
        switch (random2(10))
        {
        case 0:
            mpr("Shadowy shapes form in the air around you, then vanish.");
            break;
        case 1:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear strange voices.", MSGCH_SOUND);
            else
                mpr("You feel momentarily dizzy.");
            break;
        case 2:
            mpr("Your head hurts.");
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("Your brain hurts!");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("The world appears momentarily distorted.");
            break;
        case 7:
            mpr("Space warps around you.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            mpr("Distant voices call out to you!");
            break;
        }
        break;

    case 1:         // a little bad
        switch (random2(6))
        {
        case 0:             // identical to translocation
        case 1:
        case 2:
            mpr("You are caught in a localised spatial distortion.");
            ouch(5 + random2avg(9, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 3:
            if (create_monster(
                    mgen_data::alert_hostile_at(MONS_SPATIAL_VORTEX,
                        you.pos(), 3, flags)) != -1)
            {
                mpr("Space twists in upon itself!");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 4:
        case 5:
            if (create_monster(
                    mgen_data::alert_hostile_at(
                        summon_any_demon(DEMON_LESSER),
                        you.pos(), 5, flags)) != -1)
            {
                mpr("Something appears in a flash of light!");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 2:         // more bad
        switch (random2(6))
        {
        case 0:
        {
            bool success = false;

            for (int i = 1 + random2(3); i >= 0; --i)
            {
                if (create_monster(
                        mgen_data::alert_hostile_at(MONS_SPATIAL_VORTEX,
                            you.pos(), 3, flags)) != -1)
                {
                    success = true;
                }
            }

            if (success)
                mpr("Space twists in upon itself!");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }

        case 1:
        case 2:
            if (create_monster(
                    mgen_data::alert_hostile_at(
                        summon_any_demon(DEMON_COMMON),
                        you.pos(), 5, flags)) != -1)
            {
                mpr("Something forms out of thin air!");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;

        case 3:
        case 4:
        case 5:
        {
            bool success = false;

            for (int i = 1 + random2(2); i >= 0; --i)
            {
                if (create_monster(
                        mgen_data::alert_hostile_at(
                            summon_any_demon(DEMON_LESSER),
                            you.pos(), 5, flags)) != -1)
                {
                    success = true;
                }
            }

            if (success)
                mpr("A chorus of chattering voices calls out to you!");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        }
        break;

    case 3:         // more bad
        switch (random2(4))
        {
        case 0:
            if (create_monster(
                    mgen_data::alert_hostile_at(MONS_ABOMINATION_SMALL,
                        you.pos(), 0, flags)) != -1)
            {
                mpr("Something forms out of thin air.");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;

        case 1:
            if (create_monster(
                    mgen_data::alert_hostile_at(
                        summon_any_demon(DEMON_GREATER),
                        you.pos(), 0, flags)) != -1)
            {
                mpr("You sense a hostile presence.");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;

        case 2:
        {
            bool success = false;

            for (int i = 1 + random2(2); i >= 0; --i)
            {
                if (create_monster(
                        mgen_data::alert_hostile_at(
                            summon_any_demon(DEMON_COMMON),
                            you.pos(), 3, flags)) != -1)
                {
                    success = true;
                }
            }

            if (success)
                mpr("Something turns its malign attention towards you...");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }

        case 3:
            _send_abyss(cause);
            break;
        }
        break;
    }
}

static void _miscast_divination(int severity, const char* cause)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            mpr("Weird images run through your mind.");
            break;
        case 1:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear strange voices.", MSGCH_SOUND);
            else
                mpr("Your nose twitches.");
            break;
        case 2:
            mpr("Your head hurts.");
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("Your brain hurts!");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("Everything looks hazy for a moment.");
            break;
        case 7:
            mpr("You seem to have forgotten something, but you can't remember what it was!");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            mpr("You feel uncomfortable.");
            break;
        }
        break;

    case 1:         // more annoying things
        switch (random2(2))
        {
        case 0:
            mpr("You feel slightly disoriented.");
            forget_map(10 + random2(10));
            break;
        case 1:
            potion_effect(POT_CONFUSION, 10);
            break;
        }
        break;

    case 2:         // even more annoying things
        switch (random2(2))
        {
        case 0:
            if (you.is_undead)
                mpr("You suddenly recall your previous life!");
            else if (lose_stat(STAT_INTELLIGENCE, 1 + random2(3),
                               false, cause))
            {
                mpr("You have damaged your brain!");
            }
            else
                mpr("You have a terrible headache.");
            break;
        case 1:
            mpr("You feel lost.");
            forget_map(40 + random2(40));
            break;
        }

        potion_effect(POT_CONFUSION, 1);  // common to all cases here {dlb}
        break;

    case 3:         // nasty
        switch (random2(3))
        {
        case 0:
            mpr( forget_spell() ? "You have forgotten a spell!"
                                : "You get a splitting headache." );
            break;
        case 1:
            mpr("You feel completely lost.");
            forget_map(100);
            break;
        case 2:
            if (you.is_undead)
                mpr("You suddenly recall your previous life.");
            else if (lose_stat(STAT_INTELLIGENCE, 3 + random2(3),
                               false, cause))
            {
                mpr("You have damaged your brain!");
            }
            else
                mpr("You have a terrible headache.");
            break;
        }

        potion_effect(POT_CONFUSION, 1);  // common to all cases here {dlb}
        break;
    }
}

static void _miscast_necromancy(int severity, const char* cause)
{
    if (you.religion == GOD_KIKUBAAQUDGHA
        && (!player_under_penance() && you.piety >= piety_breakpoint(1)
            && you.piety > random2(150)))
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return;
    }

    const bool god_gift = crawl_state.is_god_acting();
    const unsigned flags = (god_gift) ? MG_GOD_GIFT : 0;

    switch (severity)
    {
    case 0:
        switch (random2(10))
        {
        case 0:
            if (player_can_smell())
                mpr("You smell decay.");
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 1:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear strange and distant voices.", MSGCH_SOUND);
            else
                mpr("You feel homesick.");
            break;
        case 2:
            mpr("Pain shoots through your body.");
            break;
        case 3:
            mpr("Your bones ache.");
            break;
        case 4:
            mpr("The world around you seems to dim momentarily.");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("You shiver with cold.");
            break;
        case 7:
            mpr("You sense a malignant aura.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            mpr("You feel very uncomfortable.");
            break;
        }
        break;

    case 1:         // a bit nasty
        switch (random2(3))
        {
        case 0:
            if (player_res_torment())
            {
                mpr("You feel weird for a moment.");
                break;
            }
            mpr("Pain shoots through your body!");
            ouch(5 + random2avg(15, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            mpr("You feel horribly lethargic.");
            potion_effect(POT_SLOWING, 15);
            break;
        case 2:
            // josh declares mummies cannot smell {dlb}
            if (player_can_smell())
            {
                mpr("You smell decay."); // identical to a harmless message
                you.rotting++;
            }
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 2:         // much nastier
        switch (random2(3))
        {
        case 0:
        {
            bool success = false;

            for (int i = random2(3); i >= 0; --i)
            {
                if (create_monster(
                        mgen_data::alert_hostile_at(MONS_SHADOW,
                            you.pos(), 2, flags)) != -1)
                {
                    success = true;
                }
            }

            if (success)
                mpr("Flickering shadows surround you.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }

        case 1:
            if (!player_prot_life() && one_chance_in(3))
            {
                drain_exp();
                break;
            }               // otherwise it just flows through...

        case 2:
            if (player_res_torment())
            {
                mpr("You feel weird for a moment.");
                break;
            }
            mpr("You convulse helplessly as pain tears through your body!");
            ouch(15 + random2avg(23, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        }
        break;

    case 3:         // even nastier
        switch (random2(6))
        {
        case 0:
            if (you.is_undead)
            {
                mpr("Something just walked over your grave. No, really!");
                break;
            }

            torment_monsters(you.x_pos, you.y_pos, 0, TORMENT_GENERIC);
            break;

        case 1:
            mpr("You are engulfed in negative energy!");

            if (!player_prot_life())
            {
                drain_exp();
                break;
            }               // otherwise it just flows through...

        case 2:
            lose_stat(STAT_RANDOM, 1 + random2avg(7, 2), false, cause);
            break;

        case 3:
            rot_player( random2avg(7, 2) + 1 );
            break;

        case 4:
            if (create_monster(
                    mgen_data::alert_hostile_at(MONS_SOUL_EATER,
                        you.pos(), 4, flags)) != -1)
            {
                mpr("Something reaches out for you...");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;

        case 5:
            if (create_monster(
                    mgen_data::alert_hostile_at(MONS_REAPER,
                        you.pos(), 4, flags)) != -1)
            {
                mpr("Death has come for you...");
            }
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;
    }
}

static void _miscast_transmigration(int severity, const char* cause)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            msg::stream << "Your " << your_hand(true)
                        << " glow momentarily." << std::endl;
            break;
        case 1:
            mpr("The air around you crackles with energy!");
            break;
        case 2:
            mpr("Multicolored lights dance before your eyes!");
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("Waves of light ripple over your body.");
            break;
        case 5:
            mpr("Strange energies run through your body.");
            break;
        case 6:
            mpr("Your skin tingles.");
            break;
        case 7:
            mpr("Your skin glows momentarily.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (player_can_smell())
                mpr("You smell something strange.");
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 1:         // slightly annoying
        switch (random2(2))
        {
        case 0:
            mpr("Your body is twisted painfully.");
            ouch(1 + random2avg(11, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            random_uselessness();
            break;
        }
        break;

    case 2:         // much more annoying
        switch (random2(4))
        {
        case 0:
            mpr("Your body is twisted very painfully!");
            ouch(3 + random2avg(23, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            mpr("You feel saturated with unharnessed energies!");
            you.magic_contamination += random2avg(19,3);
            break;
        case 2:
            potion_effect(POT_PARALYSIS, 10);
            break;
        case 3:
            potion_effect(POT_CONFUSION, 10);
            break;
        }
        break;

    case 3:         // even nastier

        switch (random2(3))
        {
        case 0:
            mpr("Your body is flooded with distortional energies!");
            you.magic_contamination += random2avg(35, 3);

            ouch(3 + random2avg(18, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;

        case 1:
            mpr("You feel very strange.");
            delete_mutation(RANDOM_MUTATION);
            ouch(5 + random2avg(23, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;

        case 2:
            mpr("Your body is distorted in a weirdly horrible way!");
            {
                const bool failMsg = !give_bad_mutation();
                if (coinflip())
                    give_bad_mutation(failMsg);
                ouch(5 + random2avg(23, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            }
            break;
        }
        break;
    }
}

static void _miscast_fire(int severity, const char* cause)
{
    bolt beam;
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            msg::stream << "Sparks fly from your " << your_hand(true)
                        << '!' << std::endl;
            break;
        case 1:
            mpr("The air around you burns with energy!");
            break;
        case 2:
            msg::stream << "Wisps of smoke drift from your "
                        << your_hand(true) << '.' << std::endl;
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            if (player_can_smell())
                mpr("You smell smoke.");
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 5:
            mpr("Heat runs through your body.");
            break;
        case 6:
            mpr("You feel uncomfortably hot.");
            break;
        case 7:
            mpr("Lukewarm flames ripple over your body.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a sizzling sound.", MSGCH_SOUND);
            else
                mpr("You feel like you have heartburn.");
            break;
        }
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            msg::stream << "Smoke pours from your "
                        << your_hand(true) << "!" << std::endl;
            big_cloud( random_smoke_type(), KC_YOU,
                       you.x_pos, you.y_pos, 20, 7 + random2(7) );
            break;

        case 1:
            mpr("Flames sear your flesh.");
            expose_player_to_element(BEAM_FIRE, 3);

            if (player_res_fire() < 0)
                ouch(2 + random2avg(13, 2), 0, KILLED_BY_WILD_MAGIC, cause);

            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("You are blasted with fire.");

            ouch( check_your_resists( 5 + random2avg(29, 2), BEAM_FIRE ), 0,
                  KILLED_BY_WILD_MAGIC, cause );

            expose_player_to_element(BEAM_FIRE, 5);
            break;

        case 1:
            mpr("You are caught in a fiery explosion!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 14 );
            beam.flavour = BEAM_FIRE;
            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "explosion";
            beam.colour = RED;
            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                beam.aux_source = cause;
            beam.ex_size = 1;
            beam.is_explosion = true;
            explosion(beam);
            break;
        }
        break;

    case 3:         // considerably less harmless stuff
        switch (random2(3))
        {
        case 0:
            mpr("You are blasted with searing flames!");

            ouch( check_your_resists( 9 + random2avg(33, 2), BEAM_FIRE ), 0,
                  KILLED_BY_WILD_MAGIC, cause );

            expose_player_to_element(BEAM_FIRE, 10);
            break;
        case 1:
            mpr("There is a sudden and violent explosion of flames!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 20 );
            beam.flavour = BEAM_FIRE;
            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "fireball";
            beam.colour = RED;
            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                beam.aux_source = cause;
            beam.ex_size = coinflip()?1:2;
            beam.is_explosion = true;
            explosion(beam);
            break;

        case 2:
            mpr("You are covered in liquid flames!");
            you.duration[DUR_LIQUID_FLAMES] += random2avg(7, 3) + 1;
            break;
        }
        break;
    }
}

static void _miscast_ice(int severity, const char* cause)
{
    bolt beam;
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            mpr("You shiver with cold.");
            break;
        case 1:
            mpr("A chill runs through your body.");
            break;
        case 2:
            msg::stream << "Wisps of condensation drift from your "
                        << your_hand(true) << "." << std::endl;
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            msg::stream << "Your " << your_hand(true)
                        << " feel numb with cold." << std::endl;
            break;
        case 5:
            mpr("A chill runs through your body.");
            break;
        case 6:
            mpr("You feel uncomfortably cold.");
            break;
        case 7:
            mpr("Frost covers your body.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a crackling sound.", MSGCH_SOUND);
            else
                mpr("A snowflake lands on your nose.");
            break;
        }
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("You feel extremely cold.");
            break;
        case 1:
            mpr("You are covered in a thin layer of ice.");
            expose_player_to_element(BEAM_COLD, 2);

            if (player_res_cold() < 0)
                ouch(4 + random2avg(5, 2), 0, KILLED_BY_WILD_MAGIC, cause);
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("Heat is drained from your body.");

            ouch(check_your_resists(5 + random2(6) + random2(7), BEAM_COLD), 0,
                 KILLED_BY_WILD_MAGIC, cause);

            expose_player_to_element(BEAM_COLD, 4);
            break;

        case 1:
            mpr("You are caught in an explosion of ice and frost!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 11 );
            beam.flavour = BEAM_COLD;
            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "explosion";
            beam.colour = WHITE;
            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                    beam.aux_source = cause;
            beam.ex_size = 1;
            beam.is_explosion = true;

            explosion(beam);
            break;
        }
        break;

    case 3:         // less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("You are blasted with ice!");

            ouch(check_your_resists(9 + random2avg(23, 2), BEAM_ICE), 0,
                 KILLED_BY_WILD_MAGIC, cause);

            expose_player_to_element(BEAM_COLD, 9);
            break;
        case 1:
            msg::stream << "Freezing gasses pour from your "
                        << your_hand(true) << "!" << std::endl;
            big_cloud(CLOUD_COLD, KC_YOU, you.x_pos, you.y_pos, 20,
                      8 + random2(4));
            break;
        }
        break;
    }
}

static void _miscast_earth(int severity, const char* cause)
{
    bolt beam;
    switch (severity)
    {
    case 0:         // just a harmless message
    case 1:
        switch (random2(10))
        {
        case 0:
            mpr("You feel earthy.");
            break;
        case 1:
            mpr("You are showered with tiny particles of grit.");
            break;
        case 2:
            msg::stream << "Sand pours from your "
                        << your_hand(true) << "." << std::endl;
            break;
        case 3:
            mpr("You feel a surge of energy from the ground.");
            break;
        case 4:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a distant rumble.", MSGCH_SOUND);
            else
                mpr("You sympathise with the stones.");
            break;
        case 5:
            mpr("You feel gritty.");
            break;
        case 6:
            mpr("You feel momentarily lethargic.");
            break;
        case 7:
            mpr("Motes of dust swirl before your eyes.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            mprf("Your %s warm.",
                (you.attribute[ATTR_TRANSFORMATION] == TRAN_AIR)
                                                    ? "lowest portion feels" :
                (!transform_changed_physiology() ?
                    (player_mutation_level(MUT_HOOVES)) ? "hooves feel" :
                    (player_mutation_level(MUT_TALONS)) ? "talons feel" :
                    (you.species == SP_NAGA)            ? "underbelly feels" :
                    (you.species == SP_MERFOLK
                        && player_is_swimming())        ? "tail feels"
                                                        : "feet feel"
                                                    : "feet feel"));
            break;
        }
        break;

    case 2:         // slightly less harmless stuff
        switch (random2(1))
        {
        case 0:
            switch (random2(3))
            {
            case 0:
                mpr("You are hit by flying rocks!");
                break;
            case 1:
                mpr("You are blasted with sand!");
                break;
            case 2:
                mpr("Rocks fall onto you out of nowhere!");
                break;
            }
            ouch( random2avg(13,2) + 10 - random2(1 + player_AC()),
                  0, KILLED_BY_WILD_MAGIC, cause);
            break;
        }
        break;

    case 3:         // less harmless stuff
        switch (random2(1))
        {
        case 0:
            mpr("You are caught in an explosion of flying shrapnel!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 15 );
            beam.flavour = BEAM_FRAG;
            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "explosion";
            beam.colour = CYAN;

            if (one_chance_in(5))
                beam.colour = BROWN;
            if (one_chance_in(5))
                beam.colour = LIGHTCYAN;

            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                beam.aux_source = cause;
            beam.ex_size = 1;
            beam.is_explosion = true;

            explosion(beam);
            break;
        }
        break;
    }
}

static void _miscast_air(int severity, const char* cause)
{
    bolt beam;
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            mpr("Ouch! You gave yourself an electric shock.");
            break;
        case 1:
            mpr("You feel momentarily weightless.");
            break;
        case 2:
            msg::stream << "Wisps of vapour drift from your "
                        << your_hand(true) << "." << std::endl;
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("You feel electric!");
            break;
        case 5:
            msg::stream << "Sparks of electricity dance between your "
                        << your_hand(true) << "." << std::endl;
            break;
        case 6:
            mpr("You are blasted with air!");
            break;
        case 7:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a whooshing sound.", MSGCH_SOUND);
            else if (player_can_smell())
                mpr("You smell ozone.");
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a crackling sound.", MSGCH_SOUND);
            else if (player_can_smell())
                mpr("You smell something musty.");
            else if (you.species == SP_MUMMY)
                mpr("Your bandages flutter.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("There is a short, sharp shower of sparks.");
            break;
        case 1:
            mprf("The wind %s around you!",
                 silenced(you.x_pos, you.y_pos) ? "whips" : "howls");
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("Electricity courses through your body.");
            ouch(check_your_resists(4 + random2avg(9, 2), BEAM_ELECTRICITY), 0,
                 KILLED_BY_WILD_MAGIC, cause);
            break;
        case 1:
            msg::stream << "Noxious gasses pour from your "
                        << your_hand(true) << "!" << std::endl;
            big_cloud(CLOUD_STINK, KC_YOU, you.x_pos, you.y_pos, 20,
                      9 + random2(4));
            break;
        }
        break;

    case 3:         // less harmless stuff
        switch (random2(2))
        {
        case 0:
            mpr("You are caught in an explosion of electrical discharges!");
            beam.type = dchar_glyph(DCHAR_FIRED_BURST);
            beam.damage = dice_def( 3, 8 );
            beam.flavour = BEAM_ELECTRICITY;
            beam.target_x = you.x_pos;
            beam.target_y = you.y_pos;
            beam.name = "explosion";
            beam.colour = LIGHTBLUE;
            beam.beam_source = NON_MONSTER;
            beam.thrower = (cause) ? KILL_MISC : KILL_YOU;
            beam.aux_source.clear();
            if (cause)
                beam.aux_source = cause;
            beam.ex_size = one_chance_in(4)?1:2;
            beam.is_explosion = true;

            explosion(beam);
            break;
        case 1:
            msg::stream << "Venomous gasses pour from your "
                        << your_hand(true) << "!" << std::endl;
            big_cloud( CLOUD_POISON, KC_YOU, you.x_pos, you.y_pos, 20,
                       8 + random2(5) );
            break;
        }
    }
}

static void _miscast_poison(int severity, const char* cause)
{
    switch (severity)
    {
    case 0:         // just a harmless message
        switch (random2(10))
        {
        case 0:
            mpr("You feel mildly nauseous.");
            break;
        case 1:
            mpr("You feel slightly ill.");
            break;
        case 2:
            msg::stream << "Wisps of poison gas drift from your "
                        << your_hand(true) << "." << std::endl;
            break;
        case 3:
            mpr("You feel a strange surge of energy!");
            break;
        case 4:
            mpr("You feel faint for a moment.");
            break;
        case 5:
            mpr("You feel sick.");
            break;
        case 6:
            mpr("You feel odd.");
            break;
        case 7:
            mpr("You feel weak for a moment.");
            break;
        case 8:
            canned_msg(MSG_NOTHING_HAPPENS);
            break;
        case 9:
            if (!silenced(you.x_pos, you.y_pos))
                mpr("You hear a slurping sound.", MSGCH_SOUND);
            else if (you.species != SP_MUMMY)
                mpr("You taste almonds.");
            else
                canned_msg(MSG_NOTHING_HAPPENS);
            break;
        }
        break;

    case 1:         // a bit less harmless stuff
        switch (random2(2))
        {
        case 0:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
            {
                mpr("You feel sick.");
                poison_player( 2 + random2(3) );
            }
            break;

        case 1:
            msg::stream << "Noxious gasses pour from your "
                        << your_hand(true) << "!" << std::endl;
            place_cloud(CLOUD_STINK, you.x_pos, you.y_pos,
                        2 + random2(4), KC_YOU);
            break;
        }
        break;

    case 2:         // rather less harmless stuff
        switch (random2(3))
        {
        case 0:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
            {
                mpr("You feel very sick.");
                poison_player( 3 + random2avg(9, 2) );
            }
            break;

        case 1:
            mpr("Noxious gasses pour from your hands!");
            big_cloud(CLOUD_STINK, KC_YOU, you.x_pos, you.y_pos, 20,
                      8 + random2(5));
            break;

        case 2:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
                lose_stat(STAT_RANDOM, 1, false, cause);
            break;
        }
        break;

    case 3:         // less harmless stuff
        switch (random2(3))
        {
        case 0:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
            {
                mpr("You feel incredibly sick.");
                poison_player( 10 + random2avg(19, 2) );
            }
            break;
        case 1:
            msg::stream << "Venomous gasses pour from your "
                        << your_hand(true) << "!" << std::endl;
            big_cloud(CLOUD_POISON, KC_YOU, you.x_pos, you.y_pos, 20,
                      7 + random2(7));
            break;
        case 2:
            if (player_res_poison())
                canned_msg(MSG_NOTHING_HAPPENS);
            else
                lose_stat(STAT_RANDOM, 1 + random2avg(5, 2), false, cause);
            break;
        }
        break;
    }
}

// sp_type:      The type of the spell.
// mag_pow:      The overall power of the spell or effect (i.e. its level).
// mag_fail:     The degree to which you failed.
// force_effect: This forces a certain severity of effect to occur.  It
//               can be disabled by being set to 100.
//
// If a god is making you miscast, any monsters produced will count as
// god gifts.
void miscast_effect(unsigned int sp_type, int mag_pow, int mag_fail,
                    int force_effect, const char *cause)
{
    if (sp_type == SPTYP_RANDOM)
        sp_type = 1 << (random2(SPTYP_LAST_EXPONENT));

    int sever = (mag_pow*mag_fail*(10+mag_pow)/7 * WILD_MAGIC_NASTINESS)/100;

    if (force_effect == 100 && random2(40) > sever && random2(40) > sever)
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return;
    }

    if (cause == NULL || strlen(cause) == 0)
        cause = "spell miscasting";

    sever /= 100;

#if DEBUG_DIAGNOSTICS
    const int old_fail = sever;
#endif

    sever = random2(sever);

    if (sever > 3)
        sever = 3;
    else if (sever < 0)
        sever = 0;

#if DEBUG_DIAGNOSTICS
    mprf(MSGCH_DIAGNOSTICS, "Sptype: %u, failure1: %d, failure2: %d",
         sp_type, old_fail, sever );
#endif

    if (force_effect != 100)
        sever = force_effect;

    switch (sp_type)
    {
    case SPTYP_CONJURATION:    _miscast_conjuration(sever, cause);    break;
    case SPTYP_ENCHANTMENT:    _miscast_enchantment(sever, cause);    break;
    case SPTYP_TRANSLOCATION:  _miscast_translocation(sever, cause);  break;
    case SPTYP_SUMMONING:      _miscast_summoning(sever, cause);      break;
    case SPTYP_DIVINATION:     _miscast_divination(sever, cause);     break;
    case SPTYP_NECROMANCY:     _miscast_necromancy(sever, cause);     break;
    case SPTYP_TRANSMIGRATION: _miscast_transmigration(sever, cause); break;
    case SPTYP_FIRE:           _miscast_fire(sever, cause);           break;
    case SPTYP_ICE:            _miscast_ice(sever, cause);            break;
    case SPTYP_EARTH:          _miscast_earth(sever, cause);          break;
    case SPTYP_AIR:            _miscast_air(sever, cause);            break;
    case SPTYP_POISON:         _miscast_poison(sever, cause);         break;
    }

    xom_is_stimulated(sever);
}

const char* failure_rate_to_string( int fail )
{
    return
        (fail == 100) ? "Useless" : // 0% success chance
        (fail > 77) ? "Terrible"  : // 0-5%
        (fail > 71) ? "Cruddy"    : // 5-10%
        (fail > 64) ? "Bad"       : // 10-20%
        (fail > 59) ? "Very Poor" : // 20-30%
        (fail > 50) ? "Poor"      : // 30-50%
        (fail > 40) ? "Fair"      : // 50-70%
        (fail > 35) ? "Good"      : // 70-80%
        (fail > 28) ? "Very Good" : // 80-90%
        (fail > 22) ? "Great"     : // 90-95%
        (fail >  0) ? "Excellent" : // 95-100%
        "Perfect";                  // 100%
}

const char* spell_hunger_string( spell_type spell )
{
    if ( you.is_undead == US_UNDEAD )
        return "N/A";

    const int hunger = spell_hunger(spell);
    if ( hunger == 0 )
        return "None";
    else if ( hunger < 25 )
        return "Minor";
    else if ( hunger < 150 )
        return "Moderate";
    else if ( hunger < 500 )
        return "Major";
    else
        return "Extreme";
}

int spell_power_colour(spell_type spell)
{
    const int powercap = spell_power_cap(spell);
    if ( powercap == 0 )
        return DARKGREY;
    const int power = calc_spell_power(spell, true);
    if ( power >= powercap )
        return WHITE;
    if ( power * 3 < powercap )
        return RED;
    if ( power * 3 < powercap * 2 )
        return YELLOW;
    return GREEN;
}

static int _power_to_barcount( int power )
{
    if (power == -1)
        return -1;

    const int breakpoints[] = { 5, 10, 15, 25, 35, 50, 75, 100, 150 };
    int result = 0;
    for (unsigned int i = 0; i < ARRAYSZ(breakpoints); ++i)
        if (power > breakpoints[i])
            ++result;

    return (result + 1);
}

int spell_power_bars( spell_type spell )
{
    const int cap = spell_power_cap(spell);
    if ( cap == 0 )
        return -1;
    const int power = std::min(calc_spell_power(spell, true), cap);
    return _power_to_barcount(power);
}

std::string spell_power_string(spell_type spell)
{
    const int numbars = spell_power_bars(spell);
    const int capbars = _power_to_barcount(spell_power_cap(spell));
    ASSERT( numbars <= capbars );
    if ( numbars < 0 )
        return "N/A";
    else
        return std::string(numbars, '#') + std::string(capbars - numbars, '.');
}
