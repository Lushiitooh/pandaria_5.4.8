/*
 * Sistema de Cartas (servidor personal de Luis).
 *
 * Cada vez que el personaje sube de nivel gana una eleccion: se le ofrecen
 * 3 cartas y escoge una. Las cartas dan mejoras pequenas que se acumulan,
 * pensadas para su clase y su raza.
 *
 *  - Rareza segun el nivel que premia la eleccion:
 *      cada 10 niveles -> Epica, cada 5 -> Rara, el resto -> Comun.
 *  - Cada carta tiene un maximo de copias (Comun 8, Rara 4, Epica 3) para que
 *    nada se dispare.
 *  - Si sube varios niveles de golpe, las elecciones quedan pendientes.
 *  - Una vez por eleccion se pueden rehacer las 3 cartas.
 *  - Personajes que ya tenian nivel reciben las elecciones de sus niveles.
 *
 * Ordenes (las usa el addon PuntosAtributo, tambien sirven escritas en el chat):
 *   .cartas            muestra la oferta actual en el chat
 *   .cartas datos      datos para el addon
 *   .cartas elegir N   escoge la carta N (1-3)
 *   .cartas rehacer    cambia las 3 cartas (1 vez por eleccion)
 *   .cartas lista      lista de cartas obtenidas
 *
 * Configuracion opcional en worldserver.conf:
 *   Cartas.Enable = 1
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Creature.h"
#include "Chat.h"
#include "Log.h"
#include "LootMgr.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdarg>

namespace Cartas
{
    enum Rareza : uint8 { COMUN = 1, RARA = 2, EPICA = 3 };

    enum Efecto : uint8
    {
        E_STAT,          // param = Stats (0..4)
        E_STATS_TODAS,   // todas las estadisticas principales
        E_VIDA,
        E_ARMADURA,
        E_RATING,        // param = RatingGrupo
        E_DANO,          // param = mascara de escuelas
        E_SANACION,
        E_ORO,
        E_XP,
    };

    enum RatingGrupo : uint8 { RG_CRITICO, RG_CELERIDAD, RG_MAESTRIA, RG_ESQUIVAR, RG_PARADA, MAX_RG };

    // Mascaras de clase (1 << (clase - 1))
    enum : uint32
    {
        GUE = 1 << 0, PAL = 1 << 1, CAZ = 1 << 2, PIC = 1 << 3, SAC = 1 << 4, CAB = 1 << 5,
        CHA = 1 << 6, MAG = 1 << 7, BRU = 1 << 8, MON = 1 << 9, DRU = 1 << 10,

        C_FUERZA    = GUE | PAL | CAB,
        C_AGILIDAD  = CAZ | PIC | CHA | MON | DRU,
        C_INTELECTO = PAL | SAC | CHA | MAG | BRU | MON | DRU,
        C_SANADOR   = PAL | SAC | CHA | MON | DRU,
        C_TANQUE    = GUE | PAL | CAB | MON | DRU,
        C_PARADA    = GUE | PAL | CAB | MON,
    };

    // Mascaras de raza (1 << (raza - 1))
    enum : uint32
    {
        HUM = 1 << 0, ORC = 1 << 1, ENA = 1 << 2, ELF = 1 << 3, NOM = 1 << 4, TAU = 1 << 5,
        GNO = 1 << 6, TRO = 1 << 7, GOB = 1 << 8, ESA = 1 << 9, DRA = 1 << 10, HUA = 1 << 21,
        PAN = (1 << 23) | (1 << 24) | (1 << 25),
    };

    // Escuelas
    enum : uint32
    {
        FIS = 1, SAG = 2, FUE = 4, NAT = 8, ESC = 16, SOM = 32, ARC = 64,
        TODO = 127,
    };

    struct Carta
    {
        uint16 id;
        uint8 rareza;
        char const* nombre;
        uint8 efecto;
        uint32 param;
        float valor;
        uint32 clases;   // 0 = todas
        uint32 razas;    // 0 = todas
    };

    static Carta const CARTAS[] =
    {
        // ---------------- Generales (todas las clases y razas) ----------------
        {   1, COMUN, "Vigor",                E_VIDA,     0, 0.5f,  0, 0 },
        {   2, COMUN, "Piel curtida",         E_ARMADURA, 0, 1.0f,  0, 0 },
        {   3, COMUN, "Ojo agudo",            E_RATING, RG_CRITICO,   0.25f, 0, 0 },
        {   4, COMUN, "Reflejos",             E_RATING, RG_CELERIDAD, 0.25f, 0, 0 },
        {   5, COMUN, "Concentraci\303\263n",        E_RATING, RG_MAESTRIA,  0.25f, 0, 0 },
        {   6, COMUN, "Determinaci\303\263n",        E_DANO,  TODO, 0.5f,  0, 0 },
        {   7, COMUN, "Aguante",              E_STAT, STAT_STAMINA, 0.5f, 0, 0 },
        {   8, COMUN, "Codicia",              E_ORO,      0, 3.0f,  0, 0 },
        {   9, COMUN, "Sabidur\303\255a",            E_XP,       0, 2.0f,  0, 0 },

        {  20, RARA,  "Coraz\303\263n robusto",      E_VIDA,     0, 1.5f,  0, 0 },
        {  21, RARA,  "Golpe certero",        E_RATING, RG_CRITICO,   0.75f, 0, 0 },
        {  22, RARA,  "\303\215mpetu",               E_RATING, RG_CELERIDAD, 0.75f, 0, 0 },
        {  23, RARA,  "Furia de batalla",     E_DANO,  TODO, 1.5f,  0, 0 },
        {  24, RARA,  "Bolsa de oro",         E_ORO,      0, 10.0f, 0, 0 },
        {  25, RARA,  "Erudito",              E_XP,       0, 5.0f,  0, 0 },
        {  26, RARA,  "Disciplina",           E_RATING, RG_MAESTRIA,  0.75f, 0, 0 },

        {  40, EPICA, "Tit\303\241n",                E_VIDA,     0, 3.0f,  0, 0 },
        {  41, EPICA, "Maestro de armas",     E_DANO,  TODO, 3.0f,  0, 0 },
        {  42, EPICA, "Instinto letal",       E_RATING, RG_CRITICO,   1.5f,  0, 0 },
        {  43, EPICA, "Tiempo acelerado",     E_RATING, RG_CELERIDAD, 1.5f,  0, 0 },

        // ---------------- Segun el rol / estadistica principal ----------------
        {  50, COMUN, "M\303\272sculos de acero",    E_STAT, STAT_STRENGTH,  0.5f, C_FUERZA, 0 },
        {  51, RARA,  "Fuerza bruta",         E_STAT, STAT_STRENGTH,  1.5f, C_FUERZA, 0 },
        {  52, EPICA, "Fuerza tit\303\241nica",      E_STAT, STAT_STRENGTH,  3.0f, C_FUERZA, 0 },
        {  53, COMUN, "Pies ligeros",         E_STAT, STAT_AGILITY,   0.5f, C_AGILIDAD, 0 },
        {  54, RARA,  "Agilidad felina",      E_STAT, STAT_AGILITY,   1.5f, C_AGILIDAD, 0 },
        {  55, EPICA, "Danza de cuchillas",   E_STAT, STAT_AGILITY,   3.0f, C_AGILIDAD, 0 },
        {  56, COMUN, "Mente aguda",          E_STAT, STAT_INTELLECT, 0.5f, C_INTELECTO, 0 },
        {  57, RARA,  "Erudici\303\263n",            E_STAT, STAT_INTELLECT, 1.5f, C_INTELECTO, 0 },
        {  58, EPICA, "Genio arcano",         E_STAT, STAT_INTELLECT, 3.0f, C_INTELECTO, 0 },
        {  59, COMUN, "Calma interior",       E_STAT, STAT_SPIRIT,    0.5f, C_SANADOR, 0 },
        {  60, COMUN, "Manos sanadoras",      E_SANACION, 0, 0.5f, C_SANADOR, 0 },
        {  61, RARA,  "Luz que reconforta",   E_SANACION, 0, 1.5f, C_SANADOR, 0 },
        {  62, EPICA, "Milagro",              E_SANACION, 0, 3.0f, C_SANADOR, 0 },
        {  63, COMUN, "Guardia firme",        E_RATING, RG_ESQUIVAR, 0.25f, C_TANQUE, 0 },
        {  64, COMUN, "Desv\303\255o",               E_RATING, RG_PARADA,   0.25f, C_PARADA, 0 },
        {  65, RARA,  "Muro de escudos",      E_ARMADURA, 0, 3.0f, C_TANQUE, 0 },
        {  66, EPICA, "Basti\303\263n",              E_VIDA,     0, 4.0f, C_TANQUE, 0 },

        // ---------------- Guerrero ----------------
        { 100, COMUN, "Filo afilado",         E_DANO, FIS, 0.75f, GUE, 0 },
        { 101, COMUN, "Grito de guerra",      E_STAT, STAT_STAMINA, 0.75f, GUE, 0 },
        { 102, RARA,  "Sed de sangre",        E_DANO, FIS, 2.0f,  GUE, 0 },
        { 103, EPICA, "Se\303\261or de la guerra",   E_DANO, FIS, 4.0f,  GUE, 0 },
        // ---------------- Paladin ----------------
        { 110, COMUN, "Luz justiciera",       E_DANO, SAG, 0.75f, PAL, 0 },
        { 111, COMUN, "Martillo bendito",     E_DANO, FIS, 0.75f, PAL, 0 },
        { 112, RARA,  "Cruzado",              E_DANO, SAG | FIS, 2.0f, PAL, 0 },
        { 113, EPICA, "Vengador de la Luz",   E_DANO, SAG | FIS, 4.0f, PAL, 0 },
        // ---------------- Cazador ----------------
        { 120, COMUN, "Punter\303\255a",             E_DANO, FIS, 0.75f, CAZ, 0 },
        { 121, COMUN, "Flechas envenenadas",  E_DANO, NAT, 0.75f, CAZ, 0 },
        { 122, RARA,  "Se\303\261or de las bestias", E_DANO, FIS | NAT, 2.0f, CAZ, 0 },
        { 123, EPICA, "Ojo de halc\303\263n",        E_DANO, TODO, 4.0f, CAZ, 0 },
        // ---------------- Picaro ----------------
        { 130, COMUN, "Venenos letales",      E_DANO, NAT, 0.75f, PIC, 0 },
        { 131, COMUN, "Pu\303\261al oculto",         E_DANO, FIS, 0.75f, PIC, 0 },
        { 132, RARA,  "Golpe en las sombras", E_DANO, FIS | NAT, 2.0f, PIC, 0 },
        { 133, EPICA, "Asesino maestro",      E_DANO, FIS | NAT, 4.0f, PIC, 0 },
        // ---------------- Sacerdote ----------------
        { 140, COMUN, "Fe ardiente",          E_DANO, SAG, 0.75f, SAC, 0 },
        { 141, COMUN, "Susurros del vac\303\255o",   E_DANO, SOM, 0.75f, SAC, 0 },
        { 142, RARA,  "Disciplina mental",    E_DANO, SAG | SOM, 2.0f, SAC, 0 },
        { 143, EPICA, "Avatar",               E_DANO, SAG | SOM, 4.0f, SAC, 0 },
        // ---------------- Caballero de la Muerte ----------------
        { 150, COMUN, "Escarcha r\303\272nica",      E_DANO, ESC, 0.75f, CAB, 0 },
        { 151, COMUN, "Plaga",                E_DANO, SOM, 0.75f, CAB, 0 },
        { 152, RARA,  "Caballero de \303\251bano",   E_DANO, FIS | ESC | SOM, 2.0f, CAB, 0 },
        { 153, EPICA, "Se\303\261or de la muerte",   E_DANO, FIS | ESC | SOM, 4.0f, CAB, 0 },
        // ---------------- Chaman ----------------
        { 160, COMUN, "Furia de la tormenta", E_DANO, NAT, 0.75f, CHA, 0 },
        { 161, COMUN, "Magma",                E_DANO, FUE, 0.75f, CHA, 0 },
        { 162, RARA,  "Se\303\261or de los elementos", E_DANO, NAT | FUE | ESC, 2.0f, CHA, 0 },
        { 163, EPICA, "Ascendencia",          E_DANO, TODO, 4.0f, CHA, 0 },
        // ---------------- Mago ----------------
        { 170, COMUN, "Escarcha profunda",    E_DANO, ESC, 0.75f, MAG, 0 },
        { 171, COMUN, "Llama viva",           E_DANO, FUE, 0.75f, MAG, 0 },
        { 172, COMUN, "Flujo arcano",         E_DANO, ARC, 0.75f, MAG, 0 },
        { 173, RARA,  "Archimago",            E_DANO, FUE | ESC | ARC, 2.0f, MAG, 0 },
        { 174, EPICA, "Tejedor de hechizos",  E_DANO, FUE | ESC | ARC, 4.0f, MAG, 0 },
        // ---------------- Brujo ----------------
        { 180, COMUN, "Sombra creciente",     E_DANO, SOM, 0.75f, BRU, 0 },
        { 181, COMUN, "Fuego vil",            E_DANO, FUE, 0.75f, BRU, 0 },
        { 182, RARA,  "Pacto oscuro",         E_DANO, SOM | FUE, 2.0f, BRU, 0 },
        { 183, EPICA, "Se\303\261or demon\303\255aco",      E_DANO, SOM | FUE, 4.0f, BRU, 0 },
        // ---------------- Monje ----------------
        { 190, COMUN, "Pu\303\261o de jade",         E_DANO, FIS, 0.75f, MON, 0 },
        { 191, COMUN, "Flujo de chi",         E_DANO, NAT, 0.75f, MON, 0 },
        { 192, RARA,  "Camino del tigre",     E_DANO, FIS | NAT, 2.0f, MON, 0 },
        { 193, EPICA, "Gran maestro",         E_DANO, FIS | NAT, 4.0f, MON, 0 },
        // ---------------- Druida ----------------
        { 200, COMUN, "Ira lunar",            E_DANO, ARC | NAT, 0.75f, DRU, 0 },
        { 201, COMUN, "Garra salvaje",        E_DANO, FIS, 0.75f, DRU, 0 },
        { 202, RARA,  "Equilibrio natural",   E_DANO, ARC | NAT | FIS, 2.0f, DRU, 0 },
        { 203, EPICA, "Encarnaci\303\263n",          E_DANO, TODO, 4.0f, DRU, 0 },

        // ---------------- Razas ----------------
        { 300, COMUN, "Versatilidad humana",  E_STATS_TODAS, 0, 0.25f, 0, HUM },
        { 301, RARA,  "Fortuna de Ventormenta", E_ORO,  0, 10.0f, 0, HUM },
        { 302, EPICA, "Hero\303\255smo de la Alianza", E_DANO, TODO, 3.0f, 0, HUM },
        { 305, COMUN, "Sangre orca",          E_DANO, TODO, 0.5f, 0, ORC },
        { 306, RARA,  "Furia sangrienta",     E_DANO, TODO, 1.5f, 0, ORC },
        { 307, EPICA, "Poder de la Horda",    E_DANO, TODO, 3.0f, 0, ORC },
        { 310, COMUN, "Piel de piedra",       E_ARMADURA, 0, 2.0f, 0, ENA },
        { 311, RARA,  "Forma de piedra",      E_VIDA,     0, 2.0f, 0, ENA },
        { 312, EPICA, "Martillo de Forjaz",   E_RATING, RG_CRITICO, 2.0f, 0, ENA },
        { 315, COMUN, "Gracia \303\251lfica",        E_RATING, RG_ESQUIVAR, 0.5f, 0, ELF },
        { 316, RARA,  "Sombra lunar",         E_RATING, RG_CELERIDAD, 1.0f, 0, ELF },
        { 317, EPICA, "Bendici\303\263n de Elune",   E_DANO, TODO, 3.0f, 0, ELF },
        { 320, COMUN, "Toque de la tumba",    E_DANO, SOM, 1.0f, 0, NOM },
        { 321, RARA,  "Voluntad renegada",    E_VIDA,     0, 2.0f, 0, NOM },
        { 322, EPICA, "Abrazo de la muerte",  E_DANO, TODO, 3.0f, 0, NOM },
        { 325, COMUN, "Resistencia taurina",  E_VIDA,     0, 1.0f, 0, TAU },
        { 326, RARA,  "Pisot\303\263n de guerra",    E_STAT, STAT_STAMINA, 2.0f, 0, TAU },
        { 327, EPICA, "Esp\303\255ritu de la tierra", E_VIDA,    0, 4.0f, 0, TAU },
        { 330, COMUN, "Mente expansiva",      E_STAT, STAT_INTELLECT, 1.0f, 0, GNO },
        { 331, RARA,  "Ingenio",              E_RATING, RG_CELERIDAD, 1.0f, 0, GNO },
        { 332, EPICA, "Genio de Gnomeregan",  E_DANO, TODO, 3.0f, 0, GNO },
        { 335, COMUN, "Regeneraci\303\263n trol",    E_VIDA,     0, 1.0f, 0, TRO },
        { 336, RARA,  "Rabiar",               E_RATING, RG_CELERIDAD, 1.0f, 0, TRO },
        { 337, EPICA, "Magia vud\303\272",           E_DANO, TODO, 3.0f, 0, TRO },
        { 340, COMUN, "Hora de pagar",        E_ORO,      0, 5.0f, 0, GOB },
        { 341, RARA,  "Mercado negro",        E_ORO,      0, 15.0f, 0, GOB },
        { 342, EPICA, "Cohete a reacci\303\263n",    E_RATING, RG_CELERIDAD, 2.0f, 0, GOB },
        { 345, COMUN, "Afinidad arcana",      E_DANO, ARC, 1.0f, 0, ESA },
        { 346, RARA,  "Torrente arcano",      E_RATING, RG_CELERIDAD, 1.0f, 0, ESA },
        { 347, EPICA, "Fuente del Sol",       E_DANO, TODO, 3.0f, 0, ESA },
        { 350, COMUN, "Don de los naaru",     E_VIDA,     0, 1.0f, 0, DRA },
        { 351, RARA,  "Presencia heroica",    E_RATING, RG_CRITICO, 1.0f, 0, DRA },
        { 352, EPICA, "Luz de los naaru",     E_DANO, TODO, 3.0f, 0, DRA },
        { 355, COMUN, "Ferocidad",            E_RATING, RG_CRITICO, 0.5f, 0, HUA },
        { 356, RARA,  "Carrera oscura",       E_RATING, RG_CELERIDAD, 1.0f, 0, HUA },
        { 357, EPICA, "Bestia interior",      E_DANO, TODO, 3.0f, 0, HUA },
        { 360, COMUN, "Panza rebotante",      E_VIDA,     0, 1.0f, 0, PAN },
        { 361, RARA,  "Gourmet",              E_XP,       0, 5.0f, 0, PAN },
        { 362, EPICA, "Equilibrio interior",  E_DANO, TODO, 3.0f, 0, PAN },
    };

    static uint8 MaxCopias(Carta const& c)
    {
        if (c.efecto == E_ORO || c.efecto == E_XP)
            return c.rareza == COMUN ? 5 : 2;
        switch (c.rareza)
        {
            case COMUN: return 8;
            case RARA:  return 4;
            default:    return 3;
        }
    }

    static Carta const* Buscar(uint32 id)
    {
        for (Carta const& c : CARTAS)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    // ------------------------------------------------------------------
    // Texto
    // ------------------------------------------------------------------
    static std::string Formato(char const* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    static std::string Num(float v)
    {
        std::string s = Formato("%.2f", v);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    static std::string Escuelas(uint32 mask)
    {
        if (mask == TODO)
            return "";
        static char const* const N[] = { "f\303\255sico", "Sagrado", "Fuego", "Naturaleza", "Escarcha", "Sombras", "Arcano" };
        std::string r;
        for (uint32 i = 0; i < 7; ++i)
        {
            if (!(mask & (1 << i)))
                continue;
            if (!r.empty())
                r += "/";
            r += N[i];
        }
        return " de " + r;
    }

    static std::string Descripcion(uint8 efecto, uint32 param, float v)
    {
        static char const* const STATS[] = { "Fuerza", "Agilidad", "Aguante", "Intelecto", "Esp\303\255ritu" };
        switch (efecto)
        {
            case E_STAT:        return "+" + Num(v) + "% de " + STATS[param < 5 ? param : 0];
            case E_STATS_TODAS: return "+" + Num(v) + "% a todas las estad\303\255sticas principales";
            case E_VIDA:        return "+" + Num(v) + "% de vida m\303\241xima";
            case E_ARMADURA:    return "+" + Num(v) + "% de armadura";
            case E_RATING:
                switch (param)
                {
                    case RG_CRITICO:   return "+" + Num(v) + "% de probabilidad de cr\303\255tico";
                    case RG_CELERIDAD: return "+" + Num(v) + "% de celeridad";
                    case RG_MAESTRIA:  return "+" + Num(v) + " de maestr\303\255a";
                    case RG_ESQUIVAR:  return "+" + Num(v) + "% de esquivar";
                    default:           return "+" + Num(v) + "% de parada";
                }
            case E_DANO:        return "+" + Num(v) + "% de da\303\261o" + Escuelas(param);
            case E_SANACION:    return "+" + Num(v) + "% de sanaci\303\263n realizada";
            case E_ORO:         return "+" + Num(v) + "% de oro de los monstruos";
            case E_XP:          return "+" + Num(v) + "% de experiencia";
        }
        return "";
    }

    static char const* NombreRareza(uint8 r)
    {
        switch (r)
        {
            case EPICA: return "|cffa335ee\303\211pica|r";
            case RARA:  return "|cff0070ddRara|r";
            default:    return "|cffffffffCom\303\272n|r";
        }
    }

    // El texto del addon usa # y ~ como separadores: se quitan de los nombres.
    static std::string Limpio(std::string s)
    {
        for (char& ch : s)
            if (ch == '#' || ch == '~' || ch == '|')
                ch = ' ';
        return s;
    }

    // ------------------------------------------------------------------
    // Datos del jugador
    // ------------------------------------------------------------------
    struct Totales
    {
        float stat[MAX_STATS] = { };
        float vida = 0.0f;
        float armadura = 0.0f;
        float rating[MAX_RG] = { };
        float dano[MAX_SPELL_SCHOOL] = { };
        float sanacion = 0.0f;
        float oro = 0.0f;
        float xp = 0.0f;
    };

    struct Datos
    {
        std::map<uint32, uint32> cantidad;    // id de carta -> copias
        uint32 elegidas = 0;
        // Oferta actual
        uint32 ofertaNivel = 0;               // nivel que premia la oferta (0 = no hay)
        uint32 oferta[3] = { 0, 0, 0 };
        bool rehecho = false;
        // Lo que esta aplicado ahora mismo sobre el personaje
        Totales aplicado;
        bool cargado = false;
    };

    static bool sActivo = true;
    static std::unordered_map<uint32, Datos> sJugadores;

    static uint32 Pendientes(Player* player, Datos const& d)
    {
        uint32 nivel = player->GetLevel();
        uint32 ganadas = nivel > 1 ? nivel - 1 : 0;
        return ganadas > d.elegidas ? ganadas - d.elegidas : 0;
    }

    // Nivel que premia la proxima eleccion (la eleccion n premia el nivel n+1)
    static uint32 NivelEleccion(Datos const& d) { return d.elegidas + 2; }

    static uint8 RarezaDeNivel(uint32 nivel)
    {
        if (nivel % 10 == 0) return EPICA;
        if (nivel % 5 == 0)  return RARA;
        return COMUN;
    }

    static Totales Calcular(Datos const& d)
    {
        Totales t;
        for (auto const& par : d.cantidad)
        {
            Carta const* c = Buscar(par.first);
            if (!c || !par.second)
                continue;
            float v = c->valor * float(par.second);
            switch (c->efecto)
            {
                case E_STAT:        if (c->param < MAX_STATS) t.stat[c->param] += v; break;
                case E_STATS_TODAS: for (uint8 i = 0; i < MAX_STATS; ++i) t.stat[i] += v; break;
                case E_VIDA:        t.vida += v; break;
                case E_ARMADURA:    t.armadura += v; break;
                case E_RATING:      if (c->param < MAX_RG) t.rating[c->param] += v; break;
                case E_DANO:
                    for (uint8 i = 0; i < MAX_SPELL_SCHOOL; ++i)
                        if (c->param & (1 << i))
                            t.dano[i] += v;
                    break;
                case E_SANACION:    t.sanacion += v; break;
                case E_ORO:         t.oro += v; break;
                case E_XP:          t.xp += v; break;
            }
        }
        return t;
    }

    static void RatingsDeGrupo(uint8 grupo, std::vector<CombatRating>& out)
    {
        switch (grupo)
        {
            case RG_CRITICO:   out = { CR_CRIT_MELEE, CR_CRIT_RANGED, CR_CRIT_SPELL }; break;
            case RG_CELERIDAD: out = { CR_HASTE_MELEE, CR_HASTE_RANGED, CR_HASTE_SPELL }; break;
            case RG_MAESTRIA:  out = { CR_MASTERY }; break;
            case RG_ESQUIVAR:  out = { CR_DODGE }; break;
            case RG_PARADA:    out = { CR_PARRY }; break;
            default:           out.clear(); break;
        }
    }

    // Aplica (apply = true) o quita los totales sobre el personaje.
    static void AplicarTotales(Player* player, Totales const& t, bool apply)
    {
        for (uint8 i = 0; i < MAX_STATS; ++i)
            if (t.stat[i] > 0.0f)
                player->HandleStatModifier(UnitMods(UNIT_MOD_STAT_START + i), TOTAL_PCT, t.stat[i], apply);
        if (t.vida > 0.0f)
            player->HandleStatModifier(UNIT_MOD_HEALTH, TOTAL_PCT, t.vida, apply);
        if (t.armadura > 0.0f)
            player->HandleStatModifier(UNIT_MOD_ARMOR, TOTAL_PCT, t.armadura, apply);

        std::vector<CombatRating> crs;
        for (uint8 g = 0; g < MAX_RG; ++g)
        {
            RatingsDeGrupo(g, crs);
            for (CombatRating cr : crs)
            {
                player->m_cartaRatingPct[cr] = apply ? t.rating[g] : 0.0f;
                player->UpdateRating(cr);
            }
        }

        for (uint8 i = 0; i < MAX_SPELL_SCHOOL; ++i)
            player->m_cartaDanoPct[i] = apply ? t.dano[i] : 0.0f;
        player->m_cartaSanacionPct = apply ? t.sanacion : 0.0f;
    }

    static void Reaplicar(Player* player, Datos& d)
    {
        AplicarTotales(player, d.aplicado, false);
        d.aplicado = Calcular(d);
        AplicarTotales(player, d.aplicado, true);
    }

    // ------------------------------------------------------------------
    // Base de datos
    // ------------------------------------------------------------------
    static void CrearTablas()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `character_cartas` ("
            "`guid` INT UNSIGNED NOT NULL,"
            "`carta` SMALLINT UNSIGNED NOT NULL,"
            "`cantidad` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY (`guid`, `carta`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Sistema de cartas: cartas elegidas'");
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `character_cartas_oferta` ("
            "`guid` INT UNSIGNED NOT NULL,"
            "`nivel` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`c1` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "`c2` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "`c3` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            "`rehecho` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY (`guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Sistema de cartas: oferta pendiente'");
    }

    static void Cargar(Player* player, Datos& d)
    {
        d.cantidad.clear();
        d.elegidas = 0;
        uint32 guid = player->GetGUIDLow();
        if (QueryResult r = CharacterDatabase.PQuery("SELECT carta, cantidad FROM character_cartas WHERE guid = %u", guid))
        {
            do
            {
                Field* f = r->Fetch();
                uint32 id = f[0].GetUInt16();
                uint32 n = f[1].GetUInt8();
                if (!Buscar(id) || !n)
                    continue;
                d.cantidad[id] = n;
                d.elegidas += n;
            } while (r->NextRow());
        }
        d.ofertaNivel = 0;
        d.rehecho = false;
        if (QueryResult r = CharacterDatabase.PQuery("SELECT nivel, c1, c2, c3, rehecho FROM character_cartas_oferta WHERE guid = %u", guid))
        {
            Field* f = r->Fetch();
            d.ofertaNivel = f[0].GetUInt32();
            d.oferta[0] = f[1].GetUInt16();
            d.oferta[1] = f[2].GetUInt16();
            d.oferta[2] = f[3].GetUInt16();
            d.rehecho = f[4].GetUInt8() != 0;
        }
        d.cargado = true;
    }

    static void GuardarOferta(Player* player, Datos const& d)
    {
        CharacterDatabase.PExecute("REPLACE INTO character_cartas_oferta (guid, nivel, c1, c2, c3, rehecho) VALUES (%u, %u, %u, %u, %u, %u)",
            player->GetGUIDLow(), d.ofertaNivel, d.oferta[0], d.oferta[1], d.oferta[2], d.rehecho ? 1 : 0);
    }

    static void GuardarCarta(Player* player, uint32 id, uint32 n)
    {
        CharacterDatabase.PExecute("REPLACE INTO character_cartas (guid, carta, cantidad) VALUES (%u, %u, %u)",
            player->GetGUIDLow(), id, n);
    }

    static Datos& Obtener(Player* player)
    {
        Datos& d = sJugadores[player->GetGUIDLow()];
        if (!d.cargado)
        {
            Cargar(player, d);
            Reaplicar(player, d);
        }
        return d;
    }

    // ------------------------------------------------------------------
    // Ofertas
    // ------------------------------------------------------------------
    static bool Valida(Player* player, Datos const& d, Carta const& c)
    {
        if (c.clases && !(c.clases & player->GetClassMask()))
            return false;
        if (c.razas && !(c.razas & player->GetRaceMask()))
            return false;
        auto it = d.cantidad.find(c.id);
        uint32 tiene = it != d.cantidad.end() ? it->second : 0;
        return tiene < MaxCopias(c);
    }

    static void GenerarOferta(Player* player, Datos& d, uint32 excluir[3])
    {
        uint8 rareza = RarezaDeNivel(NivelEleccion(d));

        // Candidatas con peso: clase x3, raza x2, general x1
        std::vector<std::pair<Carta const*, uint32>> pool;
        for (uint8 intento = 0; intento < 3 && pool.size() < 3; ++intento)
        {
            pool.clear();
            for (Carta const& c : CARTAS)
            {
                // intento 0: la rareza que toca; 1: tambien una rareza menor; 2: cualquiera
                bool rarezaOk = intento == 0 ? c.rareza == rareza
                              : intento == 1 ? (c.rareza == rareza || c.rareza + 1 == rareza)
                              : true;
                if (!rarezaOk || !Valida(player, d, c))
                    continue;
                bool excluida = false;
                for (uint8 i = 0; excluir && i < 3; ++i)
                    if (excluir[i] == c.id)
                        excluida = true;
                if (excluida)
                    continue;
                uint32 peso = c.clases ? 3 : c.razas ? 2 : 1;
                pool.emplace_back(&c, peso);
            }
            // Si al excluir las anteriores no alcanzan, se permite repetirlas
            if (pool.size() < 3 && excluir)
                excluir = nullptr, --intento;
        }

        d.oferta[0] = d.oferta[1] = d.oferta[2] = 0;
        for (uint8 k = 0; k < 3 && !pool.empty(); ++k)
        {
            uint32 total = 0;
            for (auto const& p : pool)
                total += p.second;
            uint32 tirada = urand(1, total);
            for (size_t i = 0; i < pool.size(); ++i)
            {
                if (tirada <= pool[i].second)
                {
                    d.oferta[k] = pool[i].first->id;
                    pool.erase(pool.begin() + i);
                    break;
                }
                tirada -= pool[i].second;
            }
        }
        d.ofertaNivel = NivelEleccion(d);
    }

    // Se asegura de que haya una oferta si hay elecciones pendientes
    static void PrepararOferta(Player* player, Datos& d)
    {
        if (!Pendientes(player, d))
            return;
        if (d.ofertaNivel == NivelEleccion(d) && d.oferta[0])
            return;
        d.rehecho = false;
        GenerarOferta(player, d, nullptr);
        GuardarOferta(player, d);
    }

    // ------------------------------------------------------------------
    // Mensajes
    // ------------------------------------------------------------------
    static void Mensaje(Player* player, std::string const& texto)
    {
        ChatHandler(player->GetSession()).SendSysMessage(texto.c_str());
    }

    static std::string TextoCarta(Carta const& c, uint32 copias)
    {
        Carta const& cc = c;
        std::string desc = Descripcion(cc.efecto, cc.param, cc.valor);
        if (copias)
            desc += Formato(" (tienes %u/%u)", copias, MaxCopias(cc));
        return desc;
    }

    static uint32 Copias(Datos const& d, uint32 id)
    {
        auto it = d.cantidad.find(id);
        return it != d.cantidad.end() ? it->second : 0;
    }

    // CARTAS#1#pendientes#nivelEleccion#rareza#rehacerDisponible#elegidas[#id~nombre~descripcion~rareza]x3
    static void EnviarDatosAddon(Player* player)
    {
        Datos& d = Obtener(player);
        PrepararOferta(player, d);
        uint32 pend = Pendientes(player, d);
        std::string linea = Formato("CARTAS#1#%u#%u#%u#%u#%u", pend, pend ? NivelEleccion(d) : 0,
            pend ? RarezaDeNivel(NivelEleccion(d)) : 0, (pend && !d.rehecho) ? 1 : 0, d.elegidas);
        if (pend)
        {
            for (uint8 i = 0; i < 3; ++i)
            {
                Carta const* c = Buscar(d.oferta[i]);
                if (!c)
                    continue;
                linea += "#" + Formato("%u", c->id) + "~" + Limpio(c->nombre) + "~" + Limpio(TextoCarta(*c, Copias(d, c->id)))
                    + "~" + Formato("%u", c->rareza);
            }
        }
        Mensaje(player, linea);
    }

    // CARTASL#ini / CARTASL#nombre~rareza~copias~max~totalDesc|... / CARTASL#fin
    static void EnviarListaAddon(Player* player)
    {
        Datos& d = Obtener(player);
        Mensaje(player, "CARTASL#ini");
        std::string linea;
        for (auto const& par : d.cantidad)
        {
            Carta const* c = Buscar(par.first);
            if (!c || !par.second)
                continue;
            std::string item = Limpio(c->nombre) + "~" + Formato("%u~%u~%u~", c->rareza, par.second, MaxCopias(*c))
                + Limpio(Descripcion(c->efecto, c->param, c->valor * par.second));
            if (linea.size() + item.size() > 200)
            {
                Mensaje(player, "CARTASL#" + linea);
                linea.clear();
            }
            if (!linea.empty())
                linea += "^";
            linea += item;
        }
        if (!linea.empty())
            Mensaje(player, "CARTASL#" + linea);
        Mensaje(player, "CARTASL#fin");
    }

    static void MostrarOferta(Player* player)
    {
        Datos& d = Obtener(player);
        PrepararOferta(player, d);
        uint32 pend = Pendientes(player, d);
        if (!pend)
        {
            Mensaje(player, Formato("|cffffd100[Cartas]|r No tienes cartas por elegir. Llevas %u cartas. Escribe |cffffff00.cartas lista|r para verlas.", d.elegidas));
            return;
        }
        Mensaje(player, Formato("|cffffd100[Cartas]|r Elecci\303\263n del nivel %u (%s). Pendientes: %u.", NivelEleccion(d), NombreRareza(RarezaDeNivel(NivelEleccion(d))), pend));
        for (uint8 i = 0; i < 3; ++i)
            if (Carta const* c = Buscar(d.oferta[i]))
                Mensaje(player, Formato("  |cffffff00%u)|r %s [%s]: %s", i + 1, c->nombre, NombreRareza(c->rareza), TextoCarta(*c, Copias(d, c->id)).c_str()));
        Mensaje(player, d.rehecho ? "  Escribe |cffffff00.cartas elegir 1|r, 2 o 3."
                                  : "  Escribe |cffffff00.cartas elegir 1|r, 2 o 3, o |cffffff00.cartas rehacer|r para cambiarlas (1 vez).");
    }

    static void Elegir(Player* player, uint32 opcion)
    {
        Datos& d = Obtener(player);
        PrepararOferta(player, d);
        if (!Pendientes(player, d))
        {
            Mensaje(player, "|cffffd100[Cartas]|r No tienes cartas por elegir.");
            return;
        }
        if (opcion < 1 || opcion > 3 || !Buscar(d.oferta[opcion - 1]))
        {
            Mensaje(player, "|cffff0000[Cartas]|r Opci\303\263n no v\303\241lida (1, 2 o 3).");
            return;
        }
        Carta const* c = Buscar(d.oferta[opcion - 1]);
        uint32& n = d.cantidad[c->id];
        ++n;
        ++d.elegidas;
        GuardarCarta(player, c->id, n);
        Reaplicar(player, d);
        Mensaje(player, Formato("|cffffd100[Cartas]|r Elegiste |cff00ff00%s|r: %s.", c->nombre, Descripcion(c->efecto, c->param, c->valor).c_str()));

        // Siguiente oferta (si quedan pendientes)
        d.ofertaNivel = 0;
        d.oferta[0] = d.oferta[1] = d.oferta[2] = 0;
        d.rehecho = false;
        if (Pendientes(player, d))
            PrepararOferta(player, d);
        else
            CharacterDatabase.PExecute("DELETE FROM character_cartas_oferta WHERE guid = %u", player->GetGUIDLow());
    }

    static void Rehacer(Player* player)
    {
        Datos& d = Obtener(player);
        PrepararOferta(player, d);
        if (!Pendientes(player, d))
        {
            Mensaje(player, "|cffffd100[Cartas]|r No tienes cartas por elegir.");
            return;
        }
        if (d.rehecho)
        {
            Mensaje(player, "|cffff0000[Cartas]|r Ya rehiciste las cartas de esta elecci\303\263n.");
            return;
        }
        uint32 anteriores[3] = { d.oferta[0], d.oferta[1], d.oferta[2] };
        GenerarOferta(player, d, anteriores);
        d.rehecho = true;
        GuardarOferta(player, d);
        Mensaje(player, "|cffffd100[Cartas]|r Cartas nuevas sobre la mesa.");
    }

    static void MostrarLista(Player* player)
    {
        Datos& d = Obtener(player);
        Mensaje(player, Formato("|cffffd100[Cartas]|r Tienes %u cartas:", d.elegidas));
        for (auto const& par : d.cantidad)
            if (Carta const* c = Buscar(par.first))
                Mensaje(player, Formato("  %s x%u [%s]: %s", c->nombre, par.second, NombreRareza(c->rareza),
                    Descripcion(c->efecto, c->param, c->valor * par.second).c_str()));
    }

    // Para otros scripts (oro y experiencia)
    float BonoOro(Player* player)
    {
        if (!sActivo || !player)
            return 0.0f;
        auto it = sJugadores.find(player->GetGUIDLow());
        return it != sJugadores.end() ? it->second.aplicado.oro : 0.0f;
    }

    float BonoXP(Player* player)
    {
        if (!sActivo || !player)
            return 0.0f;
        auto it = sJugadores.find(player->GetGUIDLow());
        return it != sJugadores.end() ? it->second.aplicado.xp : 0.0f;
    }
}

using namespace Cartas;

class cartas_playerscript : public PlayerScript
{
public:
    cartas_playerscript() : PlayerScript("cartas_playerscript") { }

    void OnLogin(Player* player) override
    {
        if (!sActivo)
            return;
        Datos& d = sJugadores[player->GetGUIDLow()];
        d = Datos();
        Cargar(player, d);
        d.aplicado = Calcular(d);
        AplicarTotales(player, d.aplicado, true);

        if (uint32 pend = Pendientes(player, d))
        {
            Mensaje(player, Formato("|cffffd100[Cartas]|r Tienes |cff00ff00%u|r cartas por elegir. Usa el bot\303\263n del minimapa o escribe |cffffff00.cartas|r.", pend));
            EnviarDatosAddon(player);
        }
    }

    void OnLogout(Player* player) override
    {
        sJugadores.erase(player->GetGUIDLow());
    }

    void OnLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!sActivo)
            return;
        Datos& d = Obtener(player);
        Reaplicar(player, d);   // los indices dependen del nivel
        if (player->GetLevel() > oldLevel)
        {
            Mensaje(player, Formato("|cffffd100[Cartas]|r \302\241Subiste de nivel! Tienes |cff00ff00%u|r cartas por elegir.", Pendientes(player, d)));
            EnviarDatosAddon(player);
        }
    }

    void OnGiveXP(Player* player, uint32& amount, Unit* /*victim*/) override
    {
        float bono = BonoXP(player);
        if (bono > 0.0f && amount)
            amount = uint32(float(amount) * (1.0f + bono / 100.0f));
    }
};

class cartas_worldscript : public WorldScript
{
public:
    cartas_worldscript() : WorldScript("cartas_worldscript") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        sActivo = sConfigMgr->GetBoolDefault("Cartas.Enable", true);
    }

    void OnStartup() override
    {
        sActivo = sConfigMgr->GetBoolDefault("Cartas.Enable", true);
        CrearTablas();
        TC_LOG_INFO("server.loading", ">> Sistema de Cartas: %s (%u cartas)", sActivo ? "ACTIVO" : "desactivado", uint32(sizeof(CARTAS) / sizeof(CARTAS[0])));
    }

    void OnCreatureLootGenerated(Player* looter, Creature* creature) override
    {
        float bono = BonoOro(looter);
        if (bono > 0.0f && creature && creature->loot.gold)
            creature->loot.gold = uint32(float(creature->loot.gold) * (1.0f + bono / 100.0f));
    }
};

class cartas_commandscript : public CommandScript
{
public:
    cartas_commandscript() : CommandScript("cartas_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> comandos =
        {
            { "cartas", SEC_PLAYER, false, &HandleCartas, "Sistema de cartas al subir de nivel." },
        };
        return comandos;
    }

    static bool HandleCartas(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;
        if (!sActivo)
        {
            handler->SendSysMessage("El sistema de cartas est\303\241 desactivado.");
            return true;
        }

        std::string arg = args ? args : "";
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();

        if (arg == "datos")
            EnviarDatosAddon(player);
        else if (arg == "lista")
            MostrarLista(player);
        else if (arg == "listaaddon")
            EnviarListaAddon(player);
        else if (arg == "rehacer")
        {
            Rehacer(player);
            EnviarDatosAddon(player);
        }
        else if (arg.compare(0, 6, "elegir") == 0)
        {
            unsigned int n = 0;
            sscanf(arg.c_str() + 6, "%u", &n);
            Elegir(player, n);
            EnviarDatosAddon(player);
        }
        else
            MostrarOferta(player);
        return true;
    }
};

void AddSC_sistema_cartas()
{
    sActivo = true;
    new cartas_playerscript();
    new cartas_worldscript();
    new cartas_commandscript();
}
