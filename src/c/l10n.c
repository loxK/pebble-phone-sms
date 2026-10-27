// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pebble.h>
#include "l10n.h"

static const char *const s_en[S_COUNT] = {
  [S_APP_TITLE] = "Phone",
  [S_FAVORITES] = "Favorites",
  [S_CONTACTS] = "Contacts",
  [S_RECENTS] = "Recents",
  [S_MESSAGES] = "Messages",
  [S_KEYPAD] = "Keypad",
  [S_CALL_IN_PROGRESS] = "Ongoing call",
  [S_LOADING] = "Loading...",
  [S_EMPTY] = "Nothing here",
  [S_UNREACHABLE] = "Phone unreachable",
  [S_NO_PERMISSION] = "Permission needed on the phone",
  [S_FAILED] = "Something went wrong",
  [S_MORE] = "More...",
  [S_CALL] = "Call",
  [S_SEND_SMS] = "Send an SMS",
  [S_REPLY] = "Reply",
  [S_ME] = "Me",
  [S_INCOMING] = "Incoming",
  [S_OUTGOING] = "Outgoing",
  [S_MISSED] = "Missed",
  [S_REJECTED] = "Declined",
  [S_DIALING] = "Calling...",
  [S_RINGING] = "Ringing",
  [S_ACTIVE] = "Connected",
  [S_ON_HOLD] = "On hold",
  [S_ENDED] = "Call ended",
  [S_DELETE] = "Delete",
  [S_SENT] = "Sent",
  [S_SEND_FAILED] = "Not sent",
  [S_NO_COMPANION] = "Install the Android companion app",
  [S_MUTED] = "Muted",
  [S_SPEAKER] = "Speaker",
  [S_BLUETOOTH] = "Bluetooth",
  [S_EARPIECE] = "Phone",
  [S_SEARCH] = "Search by voice",
  [S_NO_FAVORITES] = "No favorites yet. Star contacts on the phone.",
  [S_NO_RESULT] = "No match",
  [S_DICTATE] = "Dictate",
  [S_EMOJI] = "Emoji",
  [S_GLANCE_MISSED] = "Missed: ",
  // Arguments of the template format() filter that turns the seconds since the
  // event into an age, each output in double quotes.
  [S_GLANCE_AGE] = ">=1H:\"%fH hr\",>=1M:\"%fM min\",\"now\"",
  [S_PICTURE] = "[Photo]",
  [S_OPEN_ON_PHONE] = "Open on phone",
  [S_SEND_PENDING] = "Still sending on the phone",
};

static const char *const s_fr[S_COUNT] = {
  [S_APP_TITLE] = "Téléphone",
  [S_FAVORITES] = "Favoris",
  [S_CONTACTS] = "Contacts",
  [S_RECENTS] = "Récents",
  [S_MESSAGES] = "Messages",
  [S_KEYPAD] = "Clavier",
  [S_CALL_IN_PROGRESS] = "Appel en cours",
  [S_LOADING] = "Chargement...",
  [S_EMPTY] = "Rien ici",
  [S_UNREACHABLE] = "Téléphone injoignable",
  [S_NO_PERMISSION] = "Autorisation requise sur le téléphone",
  [S_FAILED] = "Une erreur est survenue",
  [S_MORE] = "Plus...",
  [S_CALL] = "Appeler",
  [S_SEND_SMS] = "Envoyer un SMS",
  [S_REPLY] = "Répondre",
  [S_ME] = "Moi",
  [S_INCOMING] = "Reçu",
  [S_OUTGOING] = "Émis",
  [S_MISSED] = "Manqué",
  [S_REJECTED] = "Refusé",
  [S_DIALING] = "Appel...",
  [S_RINGING] = "Sonnerie",
  [S_ACTIVE] = "En ligne",
  [S_ON_HOLD] = "En attente",
  [S_ENDED] = "Appel terminé",
  [S_DELETE] = "Effacer",
  [S_SENT] = "Envoyé",
  [S_SEND_FAILED] = "Échec de l'envoi",
  [S_NO_COMPANION] = "Installez l'app Android compagnon",
  [S_MUTED] = "Muet",
  [S_SPEAKER] = "Haut-parleur",
  [S_BLUETOOTH] = "Bluetooth",
  [S_EARPIECE] = "Combiné",
  [S_SEARCH] = "Recherche vocale",
  [S_NO_FAVORITES] = "Aucun favori. Ajoutez une étoile aux contacts sur le téléphone.",
  [S_NO_RESULT] = "Aucun résultat",
  [S_DICTATE] = "Dicter",
  [S_EMOJI] = "Emoji",
  [S_GLANCE_MISSED] = "Manqué : ",
  [S_GLANCE_AGE] = ">=1H:\"%fH h\",>=1M:\"%fM min\",\"à l'instant\"",
  [S_PICTURE] = "[Photo]",
  [S_OPEN_ON_PHONE] = "Ouvrir sur le téléphone",
  [S_SEND_PENDING] = "Envoi toujours en cours sur le téléphone",
};

static const char *const *s_table = s_en;

void l10n_init(void) {
  const char *locale = i18n_get_system_locale();
  s_table = strncmp(locale, "fr", 2) == 0 ? s_fr : s_en;
}

const char *tr(StringId id) {
  return s_table[id];
}
