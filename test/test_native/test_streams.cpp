// Tests natifs des décodeurs de flux et de la clé de fusion — la même
// implémentation que le firmware (include/bb_streams.h), exécutée sur le Mac.
// Lancement : pio test -e test-native
#include <unity.h>
#include <ArduinoJson.h>
#include <string>
#include <cstring>

#include "bb_streams.h"
#include "bb_emoji.h"
#include "bb_scroll.h"
#include "bb_tapback.h"

// Horloge factice : avance de 1 ms par lecture de source, pilotable.
static uint32_t sFakeNow = 0;
uint32_t bbNowMs() { return sFakeNow; }

// Source mémoire découpée en fragments : simule un flux TCP qui livre les
// octets par petits paquets arbitraires (frontières de chunks incluses).
struct MemSource {
    std::string data;
    size_t pos = 0;
    size_t maxRead;  // taille max servie par appel (fragmentation)
    explicit MemSource(std::string d, size_t frag = 7) : data(std::move(d)), maxRead(frag) {}
    size_t readBytes(char* buf, size_t len) {
        sFakeNow += 1;  // le temps passe à chaque lecture
        if (pos >= data.size()) return 0;  // fermeture de connexion
        size_t n = std::min({len, maxRead, data.size() - pos});
        memcpy(buf, data.data() + pos, n);
        pos += n;
        return n;
    }
};

// Source qui goutte indéfiniment : un octet valide par appel, sans jamais
// finir — seule une échéance absolue peut l'arrêter.
struct DripSource {
    size_t readBytes(char* buf, size_t) {
        sFakeNow += 100;  // 100 ms par octet : sous un timeout d'inactivité
        buf[0] = 'x';
        return 1;
    }
};

static const char* BODY = "{\"status\":200,\"data\":[{\"guid\":\"m1\",\"text\":\"salut\"}]}";

// Encode un corps en enveloppe chunked HTTP/1.1 (chunks de `sz` octets)
static std::string chunk(const std::string& body, size_t sz) {
    std::string out;
    for (size_t i = 0; i < body.size(); i += sz) {
        size_t n = std::min(sz, body.size() - i);
        char h[16];
        snprintf(h, sizeof(h), "%zx\r\n", n);
        out += h;
        out += body.substr(i, n);
        out += "\r\n";
    }
    out += "0\r\n\r\n";
    return out;
}

// --- BbBoundedStream --------------------------------------------------------

void test_bounded_parse_exact(void) {
    MemSource src(BODY, 5);
    BbBoundedStream<MemSource> bs(src, strlen(BODY));
    JsonDocument doc;
    auto e = deserializeJson(doc, bs);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, e.code());
    TEST_ASSERT_EQUAL_STRING("m1", doc["data"][0]["guid"] | "");
    TEST_ASSERT_TRUE(bs.drain());
    TEST_ASSERT_EQUAL(0, (int)bs.remaining());
}

void test_bounded_stops_at_limit(void) {
    // Le flux contient PLUS que la limite : la borne doit couper net.
    std::string big = std::string(BODY) + "POISON_DU_KEEPALIVE";
    MemSource src(big, 9);
    BbBoundedStream<MemSource> bs(src, strlen(BODY));
    JsonDocument doc;
    auto e = deserializeJson(doc, bs);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, e.code());
    TEST_ASSERT_EQUAL(0, (int)bs.remaining());
    // Le poison n'a pas été consommé : il appartient à la requête suivante.
    TEST_ASSERT_EQUAL('P', src.data[src.pos]);
}

void test_bounded_truncated_body(void) {
    // Connexion coupée avant la fin : l'erreur doit être IncompleteInput.
    std::string cut(BODY, 20);
    MemSource src(cut, 6);
    BbBoundedStream<MemSource> bs(src, strlen(BODY));  // longueur annoncée > reçue
    JsonDocument doc;
    auto e = deserializeJson(doc, bs);
    TEST_ASSERT_EQUAL(DeserializationError::IncompleteInput, e.code());
    TEST_ASSERT_FALSE(bs.drain());
}

// --- BbChunkedStream --------------------------------------------------------

void test_chunked_parse_multi(void) {
    for (size_t csz : {1, 3, 8, 64, 1024}) {
        for (size_t frag : {1, 4, 13}) {
            MemSource src(chunk(BODY, csz), frag);
            BbChunkedStream<MemSource> cs(src);
            JsonDocument doc;
            auto e = deserializeJson(doc, cs);
            TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, e.code(), "parse chunked");
            TEST_ASSERT_EQUAL_STRING("salut", doc["data"][0]["text"] | "");
            TEST_ASSERT_TRUE_MESSAGE(cs.drain() && cs.clean(), "0-chunk consommé");
        }
    }
}

void test_chunked_with_extension(void) {
    // Extension de chunk (« ;name=val ») : strtol s'arrête au ';', taille ok.
    std::string enc = "5;ext=1\r\nhello\r\n0\r\n\r\n";
    MemSource src(enc, 3);
    BbChunkedStream<MemSource> cs(src);
    char out[8] = {0};
    TEST_ASSERT_EQUAL(5, (int)cs.readBytes(out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("hello", out);
    TEST_ASSERT_TRUE(cs.drain());
}

void test_chunked_rejects_identity_body(void) {
    // LE bug « EmptyInput » : un corps identity (commence par '{') donné au
    // décodeur chunked. Il doit échouer proprement (eof, pas clean), et
    // c'est le peek du premier octet, côté requestJson, qui doit router ce
    // cas vers le flux identity.
    MemSource src(BODY, 7);
    BbChunkedStream<MemSource> cs(src);
    JsonDocument doc;
    auto e = deserializeJson(doc, cs);
    TEST_ASSERT_TRUE(e.code() != DeserializationError::Ok);
    TEST_ASSERT_TRUE(cs.eof());
    TEST_ASSERT_FALSE(cs.clean());
}

void test_envelope_routing(void) {
    // La discrimination par premier octet : '{' → identity, hexa → chunked.
    TEST_ASSERT_TRUE(BODY[0] == '{');
    std::string enc = chunk(BODY, 16);
    TEST_ASSERT_TRUE(isxdigit((unsigned char)enc[0]) && enc[0] != '{');
}

void test_chunked_dirty_stream(void) {
    // Chunked tronqué en plein chunk : eof + pas clean → connexion à fermer.
    std::string enc = chunk(BODY, 16);
    MemSource src(enc.substr(0, enc.size() - 12), 5);
    BbChunkedStream<MemSource> cs(src);
    JsonDocument doc;
    deserializeJson(doc, cs);
    cs.drain();
    TEST_ASSERT_FALSE(cs.clean());
}

void test_deadline_stops_dripping_source(void) {
    // Un serveur qui goutte (1 octet / 100 ms, jamais de fin) doit être coupé
    // par l'échéance absolue, pas retenu par le timeout d'inactivité.
    DripSource src;
    BbBoundedStream<DripSource> bs(src, 100000, 2000);  // budget 2 s
    char sink[64];
    size_t total = 0, r;
    while ((r = bs.readBytes(sink, sizeof(sink))) > 0) total += r;
    TEST_ASSERT_TRUE(total <= 21);  // ~20 lectures de 100 ms puis coupure
}

// --- bbBuildKey -------------------------------------------------------------

static std::string key(const char* guid) {
    char buf[96];
    bbBuildKey(guid, buf, sizeof(buf));
    return buf;
}

void test_key_french_forms_merge(void) {
    TEST_ASSERT_EQUAL_STRING("p:612345678", key("iMessage;-;+33612345678").c_str());
    TEST_ASSERT_EQUAL_STRING("p:612345678", key("SMS;-;0612345678").c_str());
    TEST_ASSERT_EQUAL_STRING("p:612345678", key("iMessage;-;+33 6 12 34 56 78").c_str());
    // iMessage et SMS d'une même personne fusionnent
    TEST_ASSERT_TRUE(key("iMessage;-;+33612345678") == key("SMS;-;0612345678"));
}

void test_key_foreign_numbers_distinct(void) {
    // Deux vrais numéros NANP différents ne fusionnent JAMAIS
    TEST_ASSERT_TRUE(key("iMessage;-;+12125551234") != key("iMessage;-;+18125551234"));
    TEST_ASSERT_EQUAL_STRING("p:12125551234", key("iMessage;-;+12125551234").c_str());
}

void test_key_short_codes(void) {
    // Numéros courts (SMS commerciaux) : forme complète, pas de collision
    TEST_ASSERT_EQUAL_STRING("p:36665", key("SMS;-;36665").c_str());
    TEST_ASSERT_TRUE(key("SMS;-;36665") != key("iMessage;-;+33612336665"));
}

void test_key_emails_and_groups(void) {
    TEST_ASSERT_EQUAL_STRING("e:jean@icloud.com", key("iMessage;-;Jean@iCloud.com").c_str());
    // Un groupe n'est jamais fusionné, quelle que soit sa forme
    TEST_ASSERT_EQUAL_STRING("g:iMessage;+;chat123456789",
                             key("iMessage;+;chat123456789").c_str());
    // E-mail et téléphone ne fusionnent pas
    TEST_ASSERT_TRUE(key("iMessage;-;jean@icloud.com") != key("iMessage;-;+33612345678"));
}

// --- segmentation émoji -----------------------------------------------------

static std::vector<BbSeg> segs(const char* s) {
    std::vector<BbSeg> out;
    size_t i = 0;
    BbSeg seg;
    while (bbNextSeg(s, strlen(s), &i, &seg)) out.push_back(seg);
    return out;
}

void test_emoji_plain_text(void) {
    auto v = segs("Salut, ça va ?");
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL(-1, v[0].glyph);
}

void test_emoji_mixed(void) {
    auto v = segs("Salut \xF0\x9F\x98\x82 !");  // "Salut 😂 !"
    TEST_ASSERT_EQUAL(3, (int)v.size());
    TEST_ASSERT_EQUAL(-1, v[0].glyph);
    TEST_ASSERT_TRUE(v[1].glyph >= 0);
    TEST_ASSERT_TRUE(v[1].glyph != kEmojiUnknown);
    TEST_ASSERT_EQUAL(-1, v[2].glyph);
}

void test_emoji_variation_selector(void) {
    // "❤️" = U+2764 U+FE0F : le sélecteur est absorbé, un seul glyphe
    auto v = segs("\xE2\x9D\xA4\xEF\xB8\x8F");
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL(bbEmojiGlyph(0x2764), v[0].glyph);
}

void test_emoji_skin_tone(void) {
    // "👍🏽" : le ton de peau est absorbé
    auto v = segs("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD");
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL(bbEmojiGlyph(0x1F44D), v[0].glyph);
}

void test_emoji_zwj_family(void) {
    // "👨‍👩‍👧" (3 membres liés par ZWJ) : replié sur un seul segment
    auto v = segs("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_TRUE(v[0].glyph >= 0);  // 👨 inconnu → glyphe « ? », mais UN seul
}

void test_emoji_unknown_becomes_placeholder(void) {
    // "🦄" : pas de glyphe dédié → « ? », jamais de tofu
    auto v = segs("\xF0\x9F\xA6\x84");
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL((int)kEmojiUnknown, v[0].glyph);
}

void test_emoji_map_sorted(void) {
    for (int i = 1; i < kEmojiMapCount; i++)
        TEST_ASSERT_TRUE(kEmojiMap[i - 1].cp < kEmojiMap[i].cp);
}

void test_emoji_accents_untouched(void) {
    // Les accents français restent du texte (pas des émojis)
    auto v = segs("éèàçù ÀÉÇ œ");
    TEST_ASSERT_EQUAL(1, (int)v.size());
    TEST_ASSERT_EQUAL(-1, v[0].glyph);
}

// ---------------------------------------------------------------------------
// Arrêts de défilement (bb_scroll.h)
// ---------------------------------------------------------------------------

// Aire de conversation réelle : 240 × 135, barres 16 + 13 → 103 px.
static const int AREA = 103;

// Conversation plus courte que l'écran : un seul arrêt, rien à défiler.
static void test_scroll_short_conversation() {
    BbBlockInfo b[] = {{22, false}, {22, false}, {19, false}};
    int s[16];
    TEST_ASSERT_EQUAL(1, bbScrollStops(b, 3, AREA, s, 16));
    TEST_ASSERT_EQUAL(0, s[0]);
}

// Le haut de la conversation doit être ATTEIGNABLE au pixel près : c'est le
// bug d'origine (arrondi en lignes → bulle du haut coupée pour toujours).
static void test_scroll_top_is_reachable() {
    BbBlockInfo b[10];
    int total = 0;
    for (int i = 0; i < 10; i++) {
        b[i].h = 25;
        b[i].header = false;
        total += 25;
    }
    int s[16];
    int n = bbScrollStops(b, 10, AREA, s, 16);
    TEST_ASSERT_TRUE(n > 1);
    TEST_ASSERT_EQUAL(total - AREA, s[n - 1]);  // haut du premier bloc = haut de zone
    TEST_ASSERT_EQUAL(0, s[0]);
    for (int i = 1; i < n; i++) TEST_ASSERT_TRUE(s[i] > s[i - 1]);  // strictement croissants
}

// Chaque arrêt aligne le haut d'une bulle sur le haut de la zone : aucune
// bulle d'ancrage n'est coupée.
static void test_scroll_stops_align_bubble_tops() {
    BbBlockInfo b[] = {{40, false}, {30, false}, {50, false}, {35, false}, {28, false}};
    int s[16];
    int n = bbScrollStops(b, 5, AREA, s, 16);
    int total = 40 + 30 + 50 + 35 + 28;
    for (int k = 1; k < n; k++) {
        // Un arrêt correspond au haut d'un bloc : total - (somme des blocs
        // sous ce bloc) - AREA.
        bool matches = false;
        int acc = 0;
        for (int i = 4; i >= 0; i--) {
            acc += b[i].h;
            if (acc - AREA == s[k]) matches = true;
        }
        TEST_ASSERT_TRUE(matches || s[k] == total - AREA);
    }
}

// Un en-tête (nom d'expéditeur, séparateur horaire) ne crée pas d'arrêt à lui
// seul : il est emporté par la bulle qu'il annonce.
static void test_scroll_header_merges_with_bubble() {
    // [bulle 60][sép 12][en-tête 11][bulle 60][bulle 40]
    BbBlockInfo b[] = {{60, false}, {12, true}, {11, true}, {60, false}, {40, false}};
    int s[16];
    int n = bbScrollStops(b, 5, AREA, s, 16);
    // L'arrêt qui montre la bulle du milieu doit inclure ses deux en-têtes :
    // acc = 40 + 60 + 11 + 12 = 123 → 123 - AREA, et non 100 - AREA.
    bool found = false;
    for (int i = 0; i < n; i++)
        if (s[i] == 123 - AREA) found = true;
    TEST_ASSERT_TRUE(found);
    for (int i = 0; i < n; i++) TEST_ASSERT_NOT_EQUAL(100 - AREA, s[i]);
}

// Une bulle plus haute que l'écran reste lisible : arrêts intermédiaires.
static void test_scroll_tall_bubble_gets_intermediate_stops() {
    BbBlockInfo b[] = {{300, false}, {30, false}};
    int s[16];
    int n = bbScrollStops(b, 2, AREA, s, 16);
    TEST_ASSERT_TRUE(n >= 3);  // pas un saut aveugle de 227 px
    for (int i = 1; i < n; i++) TEST_ASSERT_TRUE(s[i] - s[i - 1] <= AREA);
    TEST_ASSERT_EQUAL(330 - AREA, s[n - 1]);
}

// Débordement de la table : le haut reste atteignable coûte que coûte.
static void test_scroll_overflow_keeps_top_reachable() {
    BbBlockInfo b[80];
    int total = 0;
    for (int i = 0; i < 80; i++) {
        b[i].h = 30;
        b[i].header = false;
        total += 30;
    }
    int s[8];
    int n = bbScrollStops(b, 80, AREA, s, 8);
    TEST_ASSERT_TRUE(n <= 8);
    TEST_ASSERT_EQUAL(total - AREA, s[n - 1]);
}

// ---------------------------------------------------------------------------
// Normalisation des espaces (bb_emoji.h)
// Littéraux en octets explicites : une espace insécable est INVISIBLE dans le
// code source, l'écrire telle quelle rendrait le test illisible et fragile.
// ---------------------------------------------------------------------------

static std::string norm(const std::string& in) {
    std::vector<char> buf(in.begin(), in.end());
    buf.push_back(0);
    size_t n = bbNormalizeSpaces(buf.data(), in.size());
    return std::string(buf.data(), n);
}

// Le cas signalé : « Je peux t'appeler plus tard ? » avec l'espace insécable
// que met iMessage avant le « ? » — elle s'affichait en carré vide.
static void test_space_nbsp_before_question() {
    std::string s = "plus tard\xC2\xA0?";
    TEST_ASSERT_EQUAL_STRING("plus tard ?", norm(s).c_str());
}

// Fine insécable (U+202F), la variante moderne d'iOS, et cadratins.
static void test_space_narrow_and_wide() {
    TEST_ASSERT_EQUAL_STRING("a b", norm("a\xE2\x80\xAF" "b").c_str());
    TEST_ASSERT_EQUAL_STRING("a b", norm("a\xE2\x80\x82" "b").c_str());
    TEST_ASSERT_EQUAL_STRING("a b", norm("a\xE3\x80\x80" "b").c_str());
}

// Largeur nulle : supprimée, pas remplacée par une espace.
static void test_space_zero_width_removed() {
    TEST_ASSERT_EQUAL_STRING("ab", norm("a\xE2\x80\x8B" "b").c_str());
    TEST_ASSERT_EQUAL_STRING("ab", norm("\xEF\xBB\xBF" "ab").c_str());
}

// Ce qui doit rester intact : accents français, émojis, espaces normales.
static void test_space_leaves_text_intact() {
    TEST_ASSERT_EQUAL_STRING("Ça va ? Très bien !",
                             norm("Ça va ? Très bien !").c_str());
    TEST_ASSERT_EQUAL_STRING("ok \xF0\x9F\x91\x8D", norm("ok \xF0\x9F\x91\x8D").c_str());
}

// ---------------------------------------------------------------------------
// Tapbacks (bb_tapback.h)
// ---------------------------------------------------------------------------

// Les six types du sérialiseur, leurs retraits « - », et le reste rejeté
// (un sticker ou un type inconnu ne doit JAMAIS devenir une réaction).
static void test_tap_parse_types() {
    bool rm;
    TEST_ASSERT_EQUAL(0, bbTapParseType("love", &rm));
    TEST_ASSERT_FALSE(rm);
    TEST_ASSERT_EQUAL(5, bbTapParseType("question", &rm));
    TEST_ASSERT_EQUAL(3, bbTapParseType("-laugh", &rm));
    TEST_ASSERT_TRUE(rm);
    TEST_ASSERT_EQUAL(-1, bbTapParseType("sticker", &rm));
    TEST_ASSERT_EQUAL(-1, bbTapParseType("", &rm));
    TEST_ASSERT_EQUAL(-1, bbTapParseType(nullptr, &rm));
}

// Le guid cible perd son préfixe de partie, quelle qu'en soit la forme.
static void test_tap_target_strips_part_prefix() {
    TEST_ASSERT_EQUAL_STRING("ABC-123", bbTapTarget("p:0/ABC-123"));
    TEST_ASSERT_EQUAL_STRING("ABC-123", bbTapTarget("bp:2/ABC-123"));
    TEST_ASSERT_EQUAL_STRING("ABC-123", bbTapTarget("ABC-123"));
}

// Ajouts, retraits, plancher zéro : la vie d'un compteur.
static void test_tap_apply_add_remove() {
    uint8_t c[BB_TAP_TYPES] = {0};
    bbTapApply(c, 0, false);
    bbTapApply(c, 0, false);
    bbTapApply(c, 3, false);
    TEST_ASSERT_EQUAL(2, c[0]);
    TEST_ASSERT_EQUAL(1, c[3]);
    bbTapApply(c, 0, true);
    TEST_ASSERT_EQUAL(1, c[0]);
    bbTapApply(c, 3, true);
    bbTapApply(c, 3, true);  // retrait en trop : plancher 0, pas de débordement
    TEST_ASSERT_EQUAL(0, c[3]);
    bbTapApply(c, -1, false);  // type invalide : sans effet
    bbTapApply(c, 6, false);
    TEST_ASSERT_EQUAL(1, c[0]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bounded_parse_exact);
    RUN_TEST(test_bounded_stops_at_limit);
    RUN_TEST(test_bounded_truncated_body);
    RUN_TEST(test_chunked_parse_multi);
    RUN_TEST(test_chunked_with_extension);
    RUN_TEST(test_chunked_rejects_identity_body);
    RUN_TEST(test_envelope_routing);
    RUN_TEST(test_chunked_dirty_stream);
    RUN_TEST(test_deadline_stops_dripping_source);
    RUN_TEST(test_key_french_forms_merge);
    RUN_TEST(test_key_foreign_numbers_distinct);
    RUN_TEST(test_key_short_codes);
    RUN_TEST(test_key_emails_and_groups);
    RUN_TEST(test_emoji_plain_text);
    RUN_TEST(test_emoji_mixed);
    RUN_TEST(test_emoji_variation_selector);
    RUN_TEST(test_emoji_skin_tone);
    RUN_TEST(test_emoji_zwj_family);
    RUN_TEST(test_emoji_unknown_becomes_placeholder);
    RUN_TEST(test_emoji_map_sorted);
    RUN_TEST(test_emoji_accents_untouched);
    RUN_TEST(test_scroll_short_conversation);
    RUN_TEST(test_scroll_top_is_reachable);
    RUN_TEST(test_scroll_stops_align_bubble_tops);
    RUN_TEST(test_scroll_header_merges_with_bubble);
    RUN_TEST(test_scroll_tall_bubble_gets_intermediate_stops);
    RUN_TEST(test_scroll_overflow_keeps_top_reachable);
    RUN_TEST(test_space_nbsp_before_question);
    RUN_TEST(test_space_narrow_and_wide);
    RUN_TEST(test_space_zero_width_removed);
    RUN_TEST(test_space_leaves_text_intact);
    RUN_TEST(test_tap_parse_types);
    RUN_TEST(test_tap_target_strips_part_prefix);
    RUN_TEST(test_tap_apply_add_remove);
    return UNITY_END();
}
