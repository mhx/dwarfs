/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <deque>
#include <numeric>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <dwarfs/internal/fsst.h>

using namespace std::string_view_literals;
using namespace dwarfs::internal;

namespace {

constexpr std::array<std::string_view, 1000> test_strings{
    "aburabozu",
    "abuzz",
    "acacatechol",
    "acclamator",
    "accumulatively",
    "acephalan",
    "acetaldehydrase",
    "acetone",
    "ackman",
    "acquaintant",
    "acquisited",
    "acraniate",
    "acushla",
    "adamantoma",
    "addability",
    "adipomatous",
    "adjectively",
    "administrative",
    "Adramelech",
    "aku",
    "alaihi",
    "alburnum",
    "alcogene",
    "alcoholdom",
    "alcoholometric",
    "alliciency",
    "alloerotism",
    "allowedly",
    "alluring",
    "alpenhorn",
    "alphabetics",
    "alternariose",
    "Amasta",
    "amberoid",
    "ambidexterity",
    "ambient",
    "Ambystoma",
    "anaerobia",
    "analogically",
    "anamnionic",
    "Anaryan",
    "Anastasia",
    "aniseikonia",
    "Anophelinae",
    "antebridal",
    "antiministerialist",
    "antivenom",
    "antivermicular",
    "antoeci",
    "aphorize",
    "aphrizite",
    "apsidal",
    "aquopentamminecobaltic",
    "ardeb",
    "areometric",
    "argumentatory",
    "armpit",
    "arteriostenosis",
    "Arthurian",
    "Aruac",
    "asbestoidal",
    "aspirata",
    "assise",
    "astigmatical",
    "asynaptic",
    "asystolic",
    "atomician",
    "attachment",
    "attingence",
    "aurothiosulphuric",
    "autoanalytic",
    "autoinduction",
    "automata",
    "autosymbolic",
    "avenalin",
    "axmanship",
    "azine",
    "babloh",
    "babuina",
    "babyishly",
    "Bacchides",
    "bacteriform",
    "Baniva",
    "baronetical",
    "bathroomed",
    "bauta",
    "beaminess",
    "beamwork",
    "becircled",
    "bedrop",
    "bepearl",
    "bereaven",
    "besieging",
    "betinge",
    "Bharata",
    "bibliothecal",
    "bicuspid",
    "binarium",
    "birch",
    "Birkeniidae",
    "bishopling",
    "blacklegs",
    "blandness",
    "Blankit",
    "blenching",
    "blockheadedly",
    "Blumea",
    "blunderful",
    "bonnyvis",
    "boobery",
    "botanize",
    "botryoid",
    "bountifulness",
    "brachystaphylic",
    "Brachystomata",
    "branchful",
    "Branchiopoda",
    "braunite",
    "breeder",
    "brideweed",
    "broadpiece",
    "bronchomucormycosis",
    "Buddleia",
    "buffer",
    "bullionless",
    "bump",
    "burnt",
    "burtonize",
    "butlerage",
    "cacodemon",
    "calangay",
    "calfling",
    "canniness",
    "Cantabrize",
    "capocchia",
    "carapacic",
    "carnotite",
    "carpentering",
    "caryophyllous",
    "cashcuttee",
    "castlet",
    "categorist",
    "causticity",
    "cavate",
    "cavernous",
    "cecidologist",
    "centiliter",
    "cephalopathy",
    "Cercolabidae",
    "cerebrosensorial",
    "Cesare",
    "Chaouia",
    "chapatty",
    "chargeman",
    "chati",
    "chatteration",
    "cheecha",
    "chest",
    "cheve",
    "chiastic",
    "chiastoneury",
    "chlorite",
    "chronal",
    "churnmilk",
    "circuity",
    "circumoral",
    "clackety",
    "clearstarch",
    "Cleistothecopsis",
    "clifflike",
    "clitelline",
    "clithe",
    "coaration",
    "codomestication",
    "coferment",
    "cog",
    "Colchis",
    "collocatory",
    "colombier",
    "colophonium",
    "colpindach",
    "concern",
    "concessible",
    "conjugal",
    "conjury",
    "consenting",
    "constable",
    "continentality",
    "contraponend",
    "Contraposaune",
    "conventionally",
    "coprecipitation",
    "coprophilous",
    "corespect",
    "cossette",
    "cotranslator",
    "cottonbush",
    "councilorship",
    "coverchief",
    "crampy",
    "craniovertebral",
    "Craterid",
    "creamy",
    "credulity",
    "criticship",
    "cubbishly",
    "cunila",
    "Cyanastraceae",
    "cyanaurate",
    "cyanochroia",
    "cyanole",
    "cylindrograph",
    "Cypselid",
    "dakir",
    "dasturi",
    "dealkylate",
    "deaminize",
    "decretum",
    "dehors",
    "demipike",
    "Dendroidea",
    "diaphony",
    "dicing",
    "diglyceride",
    "Dioon",
    "diphenylchloroarsine",
    "Disamis",
    "disassociation",
    "discircumspection",
    "discursiveness",
    "disdiaclast",
    "disembower",
    "disfashion",
    "dishonorably",
    "disroot",
    "distastefulness",
    "distinctly",
    "distortionist",
    "disyllabic",
    "divertibility",
    "doored",
    "dorsoventral",
    "doughlike",
    "downstairs",
    "dragade",
    "drugless",
    "Dryope",
    "duke",
    "dusting",
    "earthed",
    "eatable",
    "ebracteate",
    "ectopic",
    "eelworm",
    "elect",
    "electrion",
    "electrocontractility",
    "electromerism",
    "electropotential",
    "elusive",
    "embolium",
    "emissile",
    "Emmental",
    "Empidonax",
    "emptor",
    "enclitical",
    "endotheca",
    "endurer",
    "enjoyably",
    "epistemological",
    "epizoan",
    "equalist",
    "equally",
    "equerry",
    "equiangular",
    "equinoctially",
    "equiparant",
    "Eriophorum",
    "erotic",
    "Esopus",
    "espadon",
    "ethmovomerine",
    "euphemist",
    "Europasian",
    "evagation",
    "excusator",
    "exemplarily",
    "exhalatory",
    "exiguity",
    "expectance",
    "expeditiousness",
    "extemporally",
    "fabiform",
    "facultate",
    "fallacy",
    "fantoccini",
    "fanwork",
    "fastland",
    "Faustian",
    "fawnskin",
    "fetch",
    "ficklety",
    "figurize",
    "Filipiniana",
    "fingery",
    "finiteness",
    "flayer",
    "flindosy",
    "flinger",
    "flinthearted",
    "flogging",
    "folliful",
    "foodstuff",
    "foredeck",
    "forewoman",
    "fortieth",
    "fortin",
    "fraternally",
    "freeholdership",
    "freewill",
    "Fremontia",
    "friarly",
    "Friulian",
    "fuguist",
    "fulgentness",
    "furfuraceously",
    "furiosa",
    "galant",
    "galany",
    "gastrocnemius",
    "gaywings",
    "gazelle",
    "geminiform",
    "generic",
    "geologize",
    "geophilous",
    "germal",
    "gerontocrat",
    "gien",
    "glaucin",
    "gleefully",
    "Gliridae",
    "glottogony",
    "Glyconian",
    "gnatty",
    "gobiesocid",
    "gonoplasm",
    "granula",
    "gudewife",
    "Guisard",
    "gumwood",
    "gurgle",
    "Gyges",
    "haggardly",
    "hammerwort",
    "hammochrysos",
    "hangingly",
    "haptene",
    "hardpan",
    "harr",
    "hashish",
    "hauchecornite",
    "helleborein",
    "hemiplegy",
    "hent",
    "herborization",
    "heroify",
    "Hesperian",
    "heteroerotism",
    "histology",
    "hoboism",
    "honoree",
    "hookheal",
    "horned",
    "houseminder",
    "huantajayite",
    "hubmaking",
    "hunkerous",
    "Huterian",
    "hyaenodont",
    "hybridization",
    "hydroboracite",
    "hymenopterologist",
    "hypostilbite",
    "ichthyopolist",
    "idiomorphism",
    "idoneous",
    "immanity",
    "immeritorious",
    "impartiality",
    "impersonate",
    "improvisatorially",
    "impuberal",
    "inclinableness",
    "inconsequential",
    "incopresentable",
    "incrustant",
    "incurvation",
    "indecorous",
    "indictee",
    "informant",
    "infracentral",
    "ingeldable",
    "inherently",
    "initially",
    "initiation",
    "inscribableness",
    "insocially",
    "intercreate",
    "interisland",
    "interzooecial",
    "introsentient",
    "inversed",
    "investment",
    "invigor",
    "ironheartedly",
    "isomerical",
    "isospondylous",
    "itatartrate",
    "jadery",
    "janitor",
    "Jebusi",
    "jimpness",
    "jinny",
    "Jo",
    "jugulum",
    "kale",
    "kalymmocyte",
    "kelyphite",
    "kerbstone",
    "kettle",
    "khedive",
    "Koelreuteria",
    "Koreshan",
    "kuttar",
    "lairdess",
    "Lappish",
    "latch",
    "Latinize",
    "laudatorily",
    "laumontite",
    "lavaret",
    "leaky",
    "legislative",
    "legislatorial",
    "leoncito",
    "leopard",
    "lipoid",
    "liroconite",
    "livingness",
    "loasaceous",
    "loathness",
    "logarithmetically",
    "logorrhea",
    "loquacious",
    "lotto",
    "lowerable",
    "lycoperdaceous",
    "maintainer",
    "Malaclemys",
    "mammalogist",
    "maney",
    "Margery",
    "marron",
    "mastoidohumeral",
    "mauger",
    "mazzard",
    "meered",
    "melicerous",
    "meningomyelitis",
    "merocrystalline",
    "mesogyrate",
    "mesolabe",
    "mesothermal",
    "metacresol",
    "meteorical",
    "metronomic",
    "Michigander",
    "microchemical",
    "micropolariscope",
    "microtomic",
    "mildewer",
    "misdo",
    "misemphasis",
    "misgovernance",
    "misrender",
    "monoid",
    "mooncreeper",
    "moratory",
    "morbidity",
    "mottramite",
    "moundlet",
    "muleman",
    "multiplex",
    "multitudinal",
    "musquaw",
    "myope",
    "Myrcia",
    "mythogonic",
    "Nabalitic",
    "nailproof",
    "naipkin",
    "nasociliary",
    "Nearctica",
    "neophilological",
    "neuromyelitis",
    "nickelic",
    "nidology",
    "niello",
    "niggardize",
    "nonacquittal",
    "nonadult",
    "noncoloring",
    "nonconducive",
    "noncreeping",
    "noncurling",
    "nondegeneration",
    "nongraduated",
    "nonheritor",
    "nonoccupation",
    "nonplanar",
    "nonprevalence",
    "nonretiring",
    "nonrhyming",
    "nonsecretory",
    "nonspecial",
    "nonsubstantiation",
    "norbergite",
    "Notus",
    "nucleon",
    "number",
    "nuncupatively",
    "nymphid",
    "Observantist",
    "odontonosology",
    "offendant",
    "Oklahoma",
    "oligosite",
    "omniparity",
    "oncosis",
    "ophthalmiatrics",
    "ophthalmitis",
    "opposure",
    "orendite",
    "Orientalia",
    "ornithosaurian",
    "orthosemidine",
    "orthotactic",
    "Oryza",
    "oscheocele",
    "osse",
    "ostempyesis",
    "ostreoid",
    "Otariinae",
    "outcropper",
    "outsmart",
    "outsuck",
    "outwander",
    "overcolor",
    "overdeeming",
    "overdrowsed",
    "overjawed",
    "overpitched",
    "overpole",
    "overremissness",
    "overspring",
    "oversqueak",
    "oversystematic",
    "overtrump",
    "oxberry",
    "oxyketone",
    "palpiform",
    "Panak",
    "pancreatotomy",
    "Panorpidae",
    "Pantagruel",
    "Pantagruelically",
    "pantamorphic",
    "pantochromism",
    "pantophile",
    "papaverous",
    "Paradoxides",
    "paranymphal",
    "parasitotropic",
    "parfilage",
    "Parnassus",
    "partisan",
    "partitive",
    "pathoanatomical",
    "pauseful",
    "pedagogism",
    "Pedetidae",
    "pejorate",
    "pelican",
    "pelmatogram",
    "peltiferous",
    "pendragon",
    "pensive",
    "pentaspherical",
    "Percheron",
    "periphyllum",
    "peritomize",
    "peritonsillitis",
    "pervasively",
    "Petiolata",
    "phalarope",
    "pharmacognosia",
    "Phenalgin",
    "philomystic",
    "Pholadacea",
    "phonophotography",
    "photographize",
    "photolysis",
    "photometrograph",
    "phraseologically",
    "phrenic",
    "Phyteus",
    "phytomorphic",
    "pietistic",
    "pikle",
    "pinacone",
    "pinsons",
    "plasterer",
    "play",
    "plenicorn",
    "pleomastia",
    "plessimeter",
    "pleuroperitonaeal",
    "plexiform",
    "plumade",
    "pluviometrical",
    "pneumony",
    "podder",
    "podophthalmitic",
    "pokable",
    "Polistes",
    "porcellanid",
    "postspinous",
    "potto",
    "powwower",
    "praesystolic",
    "pram",
    "preaccomplishment",
    "preanterior",
    "preboding",
    "precordiality",
    "predeficient",
    "pregranite",
    "prehistorics",
    "preliability",
    "premeditative",
    "prepatriotic",
    "presbytic",
    "prespecialist",
    "proceed",
    "Proctotrypidae",
    "proextension",
    "profitlessness",
    "projecture",
    "promptbook",
    "proreduction",
    "prosodiac",
    "protomorph",
    "protosiphonaceous",
    "provoker",
    "proxenos",
    "proximally",
    "Prunella",
    "prunelle",
    "pseudocartilaginous",
    "Pseudopeziza",
    "pseudosocialistic",
    "pseudosyllogism",
    "psychoautomatic",
    "Pteranodon",
    "Ptolemaic",
    "pulverization",
    "pyrochlore",
    "quibble",
    "quinize",
    "quintette",
    "quintile",
    "Rajah",
    "Rastaban",
    "rebato",
    "Rebecca",
    "rebolt",
    "reburn",
    "recarburization",
    "receptionism",
    "recession",
    "recipient",
    "redjacket",
    "reflorescent",
    "refusion",
    "regimentalled",
    "Reichslander",
    "remilitarize",
    "remindal",
    "renomination",
    "repersuade",
    "repertorium",
    "replenisher",
    "representable",
    "reprise",
    "reserved",
    "resmell",
    "reticulovenose",
    "retrace",
    "retraxit",
    "retrenchable",
    "reventilate",
    "rhabdomal",
    "Rhaetian",
    "rhubarb",
    "Rhynchospora",
    "Ribhus",
    "ricksha",
    "rimose",
    "Russolatry",
    "saccharomyces",
    "saddlery",
    "sagacious",
    "samkara",
    "sauntering",
    "Sciarinae",
    "scoon",
    "scranning",
    "scribblatory",
    "scride",
    "Scriptureless",
    "scullionish",
    "seamanship",
    "seashore",
    "sedentarily",
    "selvaged",
    "sematic",
    "semiantique",
    "semicollar",
    "semigenuflection",
    "semiorb",
    "semiordinate",
    "semioxidated",
    "semiproof",
    "semiquadrantly",
    "semisociative",
    "semitheological",
    "semuncia",
    "sensal",
    "septarian",
    "seriation",
    "serpentina",
    "serranoid",
    "shaftman",
    "Shakespeareana",
    "shandrydan",
    "sheepbiter",
    "Shetlandic",
    "shoddywards",
    "showless",
    "sifting",
    "signifier",
    "sinoauricular",
    "siphonapterous",
    "siphonosome",
    "sittee",
    "smellage",
    "Smyrniot",
    "sniffing",
    "snubbishness",
    "soapberry",
    "sociologizer",
    "softball",
    "solemnize",
    "solitudinize",
    "somatical",
    "somnolently",
    "sooky",
    "soonish",
    "sparsely",
    "spathed",
    "speechmaking",
    "spellword",
    "Sphaerocarpus",
    "sphindid",
    "splanchnodynia",
    "splenocyte",
    "spondylexarthrosis",
    "spongiolin",
    "sporeling",
    "spotted",
    "squireless",
    "stachys",
    "Stalinism",
    "stampweed",
    "stannate",
    "stanner",
    "statesmanship",
    "stauracin",
    "stenosed",
    "stereoscopically",
    "stickwater",
    "Stilophora",
    "stimulability",
    "stonify",
    "storkish",
    "stoutly",
    "stove",
    "strenuousness",
    "strongbox",
    "sturdiness",
    "sufflation",
    "sulfamethazine",
    "sunshining",
    "supercarbonate",
    "superfluousness",
    "superfortunate",
    "superreliance",
    "supramaxilla",
    "surinamine",
    "surprisable",
    "surrebut",
    "swapping",
    "Swazi",
    "swingable",
    "Synchytrium",
    "syndesmology",
    "syntaxist",
    "tabor",
    "tairn",
    "tangle",
    "Tantony",
    "tartaret",
    "teammate",
    "tearable",
    "telecommunication",
    "telford",
    "tempre",
    "tender",
    "testicle",
    "thegnly",
    "theoretician",
    "theosophism",
    "Thiobacillus",
    "throatroot",
    "Thunnidae",
    "tidewater",
    "Timonian",
    "Timuquan",
    "tolerable",
    "tonicobalsamic",
    "tonsillectomize",
    "toolstock",
    "tournant",
    "trabacolo",
    "tragicomicality",
    "tramway",
    "translative",
    "transmigrationist",
    "trianthous",
    "trichitis",
    "tricoryphean",
    "trimesitic",
    "trionychoid",
    "tristichous",
    "trona",
    "Tsuga",
    "turbaned",
    "turkeyberry",
    "twangy",
    "ultraconfident",
    "ultraconservative",
    "Ulvales",
    "unalimentary",
    "unamply",
    "unauthentic",
    "unbold",
    "unceremented",
    "uncially",
    "uncompact",
    "unconcernment",
    "unconsoling",
    "uncultured",
    "undecreed",
    "undefinedly",
    "undeformedness",
    "undenominated",
    "undercharged",
    "underpassion",
    "undevelopable",
    "unduncelike",
    "unduty",
    "unexcitable",
    "unfanned",
    "unfence",
    "unfighting",
    "unglorious",
    "ungrow",
    "unhaste",
    "unifocal",
    "unilabiated",
    "unimperialistic",
    "unimposedly",
    "unincarnate",
    "unliquid",
    "unmechanize",
    "unmellowed",
    "unmistakable",
    "unmuddle",
    "unnagging",
    "unnegotiableness",
    "unobstruct",
    "unobtrusiveness",
    "unorganically",
    "unperishably",
    "unplacid",
    "unpolled",
    "unpossessed",
    "unprivileged",
    "unpronounced",
    "unproportioned",
    "unpurged",
    "unreclined",
    "unregretted",
    "unremittingness",
    "unrepresentedness",
    "unruled",
    "unsalutary",
    "unsalvability",
    "unsanctify",
    "unsaponified",
    "unseated",
    "unseldom",
    "unshavenly",
    "unsolvable",
    "unstopper",
    "unsung",
    "unsupplicated",
    "untaintedness",
    "untenty",
    "unthaw",
    "untrainedly",
    "untranspassable",
    "unvetoed",
    "unvocalized",
    "unwalled",
    "unwarlike",
    "unwhisked",
    "unwrathful",
    "uphoard",
    "upprick",
    "uptrain",
    "upwork",
    "upwreathe",
    "urbanely",
    "ureometry",
    "ureterocystoscope",
    "urethroscopy",
    "urological",
    "urticarial",
    "usara",
    "vacantry",
    "vaccinogenous",
    "valeramide",
    "valonia",
    "vanillinic",
    "velate",
    "viewless",
    "visceroparietal",
    "vituperative",
    "vocably",
    "volatilizable",
    "voucher",
    "Wagneriana",
    "waketime",
    "walleye",
    "wappenschaw",
    "waxweed",
    "wear",
    "weatherer",
    "weave",
    "werewolfism",
    "wheem",
    "whippletree",
    "whistlewing",
    "whom",
    "wicking",
    "widowership",
    "windwayward",
    "wisehearted",
    "workbench",
    "worldish",
    "worsening",
    "xenian",
    "yachting",
    "Yugoslavic",
    "zebu",
    "zimme",
    "zoocurrent",
    "zoopraxiscope",
};

constexpr auto total_string_length = std::accumulate(
    test_strings.begin(), test_strings.end(), 0,
    [](size_t sum, std::string_view str) { return sum + str.size(); });

class chunked_strings {
 public:
  using value_type = std::u8string;

  static constexpr std::size_t chunk_elements{64};

  void push_back(std::u8string s) {
    if (chunks_.empty() || chunks_.back().size() == chunk_elements) {
      chunks_.emplace_back();
      chunks_.back().reserve(chunk_elements);
    }
    chunks_.back().push_back(std::move(s));
    ++size_;
  }

  std::u8string const& operator[](std::size_t i) const {
    return chunks_[i / chunk_elements][i % chunk_elements];
  }

  std::size_t size() const { return size_; }
  std::size_t num_chunks() const { return chunks_.size(); }

  std::span<std::u8string const> chunk(std::size_t c) const {
    return {chunks_[c].data(), chunks_[c].size()};
  }

  void release_chunk(std::size_t c) {
    chunks_[c].clear();
    chunks_[c].shrink_to_fit();
  }

 private:
  std::deque<std::vector<std::u8string>> chunks_;
  std::size_t size_{0};
};

std::vector<std::u8string> u8_test_strings(std::size_t count, unsigned seed) {
  std::mt19937 rng{seed};
  std::vector<std::u8string> result;
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto const& s = test_strings[rng() % test_strings.size()];
    result.emplace_back(reinterpret_cast<char8_t const*>(s.data()), s.size());
  }
  return result;
}

std::size_t total_size(std::vector<std::u8string> const& v) {
  return std::accumulate(
      v.begin(), v.end(), std::size_t{0},
      [](std::size_t n, auto const& s) { return n + s.size(); });
}

void expect_same_result(fsst_encoder::bulk_compression_result const& expected,
                        fsst_encoder::bulk_compression_result const& actual) {
  EXPECT_EQ(expected.dictionary, actual.dictionary);
  EXPECT_EQ(expected.buffer, actual.buffer);
  ASSERT_EQ(expected.positions.size(), actual.positions.size());
  for (std::size_t i = 0; i < expected.positions.size(); ++i) {
    ASSERT_EQ(expected.positions[i], actual.positions[i]) << "at index " << i;
  }
}

void expect_round_trips(fsst_encoder::bulk_compression_result const& r,
                        std::vector<std::u8string> const& input) {
  fsst_decoder const decoder{r.dictionary};
  ASSERT_EQ(input.size() + 1, r.positions.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    auto const begin = r.positions[i];
    auto const end = r.positions[i + 1];
    std::u8string_view const compressed{
        reinterpret_cast<char8_t const*>(r.buffer.data()) + begin, end - begin};
    EXPECT_EQ(input[i], decoder.decompress(compressed)) << "at index " << i;
  }
}

} // namespace

TEST(fsst_test, basic) {
  auto const res = fsst_encoder::compress(test_strings);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(test_strings.size() + 1, res->positions.size());
  EXPECT_GT(res->dictionary.size(), 550);
  EXPECT_LT(res->dictionary.size(), 600);
  EXPECT_LT(res->buffer.size(), 9 * total_string_length / 17);

  auto const decoder = fsst_decoder{res->dictionary};

  for (size_t i = 0; i < test_strings.size(); ++i) {
    auto const& str = test_strings[i];
    auto const compressed_data = std::string_view{res->buffer}.substr(
        res->positions[i], res->positions[i + 1] - res->positions[i]);

    auto const decompressed = decoder.decompress(compressed_data);

    EXPECT_EQ(str, decompressed);
  }
}

TEST(fsst_random_test, random_strings) {
#ifdef DWARFS_TEST_CROSS_COMPILE
  static constexpr int num_random_tests = 100;
#else
  static constexpr int num_random_tests = 1000;
#endif

  std::mt19937 rng{42};
  std::uniform_int_distribution<size_t> sample_size_dist(0, 100);
  std::vector<size_t> sample_sizes;

  sample_sizes.reserve(num_random_tests);
  sample_sizes.push_back(0); // Definitely include the empty set
  std::ranges::generate_n(std::back_inserter(sample_sizes),
                          num_random_tests - 1,
                          [&]() { return sample_size_dist(rng); });

  for (auto const sample_size : sample_sizes) {
    std::vector<std::string_view> input(sample_size);
    std::ranges::sample(test_strings, input.begin(), input.size(), rng);

    auto const res = fsst_encoder::compress(input, true);
    auto const res2 = fsst_encoder::compress(input);

    if (sample_size == 0) {
      ASSERT_FALSE(res.has_value());
      ASSERT_FALSE(res2.has_value());
    } else {
      ASSERT_TRUE(res.has_value());
      EXPECT_EQ(input.size() + 1, res->positions.size());

      auto const total_input_length =
          std::accumulate(input.begin(), input.end(), size_t{0},
                          [](size_t n, auto const& s) { return n + s.size(); });

      if (res->dictionary.size() + res->buffer.size() < total_input_length) {
        EXPECT_TRUE(res2.has_value());
      } else {
        EXPECT_FALSE(res2.has_value());
      }

      if (sample_size >= 500) {
        EXPECT_LE(res->buffer.size(), 60 * total_input_length / 100);
      } else if (sample_size >= 200) {
        EXPECT_LE(res->buffer.size(), 70 * total_input_length / 100);
      } else if (sample_size >= 100) {
        EXPECT_LE(res->buffer.size(), 100 * total_input_length / 100);
      } else if (sample_size >= 20) {
        EXPECT_LE(res->buffer.size(), 120 * total_input_length / 100);
      } else {
        EXPECT_LE(res->buffer.size(), 200 * total_input_length / 100);
      }

      auto const decoder = fsst_decoder{res->dictionary};

      for (size_t i = 0; i < input.size(); ++i) {
        auto const& str = input[i];
        auto const compressed_data = std::string_view{res->buffer}.substr(
            res->positions[i], res->positions[i + 1] - res->positions[i]);

        auto const decompressed = decoder.decompress(compressed_data);

        EXPECT_EQ(str, decompressed);
      }
    }
  }
}

class fsst_incremental_test : public ::testing::TestWithParam<std::size_t> {};

TEST_P(fsst_incremental_test, matches_one_shot) {
  auto const batch_size = GetParam();
  auto const input = u8_test_strings(5000, 1);

  auto const one_shot = fsst_encoder::compress(input, true);
  ASSERT_TRUE(one_shot.has_value());

  auto compressor = fsst_incremental_compressor<std::u8string>::create(input);

  for (std::size_t i = 0; i < input.size(); i += batch_size) {
    auto const n = std::min(batch_size, input.size() - i);
    compressor.add(std::span<std::u8string const>{input.data() + i, n});
  }

  EXPECT_EQ(input.size(), compressor.count());
  EXPECT_EQ(total_size(input), compressor.input_size());
  EXPECT_EQ(one_shot->buffer.size(), compressor.compressed_size());
  EXPECT_EQ(one_shot->dictionary.size(), compressor.dictionary_size());

  auto const result = std::move(compressor).finish();

  expect_same_result(*one_shot, result);
  expect_round_trips(result, input);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, fsst_incremental_test,
                         ::testing::Values(1, 2, 7, 64, 1000, 100000),
                         ::testing::PrintToStringParamName());

TEST(fsst_incremental_test, source_may_be_released_between_batches) {
  auto const input = u8_test_strings(5000, 2);

  auto const reference = fsst_encoder::compress(input, true);
  ASSERT_TRUE(reference.has_value());

  chunked_strings source;
  for (auto const& s : input) {
    source.push_back(s);
  }
  ASSERT_GT(source.num_chunks(), 1);

  auto compressor = fsst_incremental_compressor<std::u8string>::create(source);

  for (std::size_t c = 0; c < source.num_chunks(); ++c) {
    compressor.add(source.chunk(c));
    source.release_chunk(c);
  }

  auto const result = std::move(compressor).finish();

  expect_same_result(*reference, result);
  expect_round_trips(result, input);
}

TEST(fsst_incremental_test, estimate_does_not_affect_the_result) {
  auto const input = u8_test_strings(20000, 3);

  auto const reference = fsst_encoder::compress(input, true);
  ASSERT_TRUE(reference.has_value());

  auto compressor = fsst_incremental_compressor<std::u8string>::create(input);

  std::mt19937 rng{4};
  std::vector<std::u8string_view> sample;
  sample.reserve(500);
  for (int i = 0; i < 500; ++i) {
    sample.emplace_back(input[rng() % input.size()]);
  }

  auto const sample_input =
      std::accumulate(sample.begin(), sample.end(), std::size_t{0},
                      [](std::size_t n, auto s) { return n + s.size(); });
  auto const sample_output =
      compressor.estimated_compressed_size(fsst_string_source{sample});

  EXPECT_GT(sample_output, 0);
  EXPECT_EQ(0, compressor.count());
  EXPECT_EQ(0, compressor.compressed_size());

  compressor.add(input);
  auto const result = std::move(compressor).finish();

  // estimating must not have contributed anything to the result
  expect_same_result(*reference, result);

  auto const predicted = static_cast<double>(sample_output) /
                         static_cast<double>(sample_input) *
                         static_cast<double>(total_size(input));
  auto const actual = static_cast<double>(result.buffer.size());

  EXPECT_NEAR(1.0, predicted / actual, 0.1);
}

TEST(fsst_incremental_test, empty_batches_are_ignored) {
  auto const input = u8_test_strings(100, 5);

  auto const reference = fsst_encoder::compress(input, true);
  ASSERT_TRUE(reference.has_value());

  auto compressor = fsst_incremental_compressor<std::u8string>::create(input);

  compressor.add(std::span<std::u8string const>{});
  compressor.add(input);
  compressor.add(std::span<std::u8string const>{});

  auto const result = std::move(compressor).finish();

  expect_same_result(*reference, result);
}

TEST(fsst_incremental_test, handles_strings_longer_than_the_sample_line) {
  std::vector<std::u8string> input;
  std::mt19937 rng{6};
  for (int i = 0; i < 200; ++i) {
    std::u8string s;
    for (int k = 0, n = 60 + static_cast<int>(rng() % 80); k < n; ++k) {
      auto const& w = test_strings[rng() % test_strings.size()];
      s.append(reinterpret_cast<char8_t const*>(w.data()), w.size());
    }
    input.emplace_back(std::move(s));
  }

  auto const one_shot = fsst_encoder::compress(input, true);
  ASSERT_TRUE(one_shot.has_value());

  auto compressor = fsst_incremental_compressor<std::u8string>::create(input);
  for (auto const& s : input) {
    compressor.add(std::span<std::u8string const>{&s, 1});
  }
  auto const result = std::move(compressor).finish();

  expect_same_result(*one_shot, result);
  expect_round_trips(result, input);
}

// A long string that compresses well must not be given up on.
//
// `compressBulk()` splits each string into pieces of at most 511 bytes and
// needs `2 * piece + 7` bytes of output space available for each one. The
// previous implementation sized its output buffer to the uncompressed length,
// which does not satisfy that for an input of a few hundred bytes containing a
// string longer than 511 bytes: `fsst_compress()` could make no progress, and
// `compress()` returned nullopt even where compressing was clearly profitable.
//
// A repetitive string is used deliberately. For a single string of this length
// the dictionary is a large part of the total cost, so a string of *diverse*
// words this short is genuinely not worth compressing -- nullopt would be the
// right answer there, and the test would prove nothing.
TEST(fsst_incremental_test, long_compressible_strings_are_not_given_up_on) {
  for (std::size_t target : {520, 600, 800, 1000, 1100}) {
    std::u8string s;
    while (s.size() < target) {
      s += u8"aburabozu/";
    }
    ASSERT_GT(s.size(), 511) << "must exceed FSST_SAMPLELINE";

    std::vector<std::u8string> const input{s};

    auto const forced = fsst_encoder::compress(input, true);
    ASSERT_TRUE(forced.has_value());
    ASSERT_LT(forced->dictionary.size() + forced->buffer.size(), s.size())
        << "a repetitive string of " << s.size() << " bytes should compress";

    auto const unforced = fsst_encoder::compress(input);
    ASSERT_TRUE(unforced.has_value()) << "at length " << s.size();

    expect_same_result(*forced, *unforced);
    expect_round_trips(*unforced, input);
  }
}

// Whether the unforced call returns a result must depend only on whether
// compressing actually pays off, never on an implementation limit. This is the
// property the old output-buffer sizing violated, and it holds regardless of
// which inputs happen to be profitable.
TEST(fsst_incremental_test, unforced_result_matches_the_forced_decision) {
  std::mt19937 rng{8};

  for (std::size_t count : {1, 2, 5, 50, 500}) {
    for (std::size_t words : {1, 10, 60, 200}) {
      std::vector<std::u8string> input;
      input.reserve(count);

      for (std::size_t i = 0; i < count; ++i) {
        std::u8string s;
        for (std::size_t k = 0; k < words; ++k) {
          auto const& w = test_strings[rng() % test_strings.size()];
          s.append(reinterpret_cast<char8_t const*>(w.data()), w.size());
        }
        input.emplace_back(std::move(s));
      }

      auto const forced = fsst_encoder::compress(input, true);
      ASSERT_TRUE(forced.has_value())
          << "count=" << count << ", words=" << words;

      auto const unforced = fsst_encoder::compress(input);
      auto const worthwhile =
          forced->dictionary.size() + forced->buffer.size() < total_size(input);

      EXPECT_EQ(worthwhile, unforced.has_value())
          << "count=" << count << ", words=" << words;
    }
  }
}
