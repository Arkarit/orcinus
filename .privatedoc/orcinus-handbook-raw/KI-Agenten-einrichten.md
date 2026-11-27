# KI-Agenten einrichten

## Vor dem ersten Gespräch

Die Seitenleiste „KI-Assistent“ redet mit einem KI-Agenten, der auf Deinem Rechner läuft. Orcinus selbst bringt keinen Agenten mit und ruft keinen KI-Dienst auf: Du richtest einmal einen Agenten ein, und ab dann startet Orcinus ihn, sobald Du ihm schreibst. Was die Seitenleiste alles kann, steht im Kapitel „KI-Unterstützung“.

Es gibt zwei Wege: einen **Cloud-Agenten** (Claude Code oder Codex, mit Deinem Konto bei Anthropic oder OpenAI) und ein **lokales Modell** (auf Deiner eigenen Grafikkarte, über LM Studio oder Strata und OpenCode). Jeder andere Agent, der das Agent Client Protocol (ACP) spricht, funktioniert ebenfalls.

Und ja, das ernste Wort aus dem Kapitel „KI-Unterstützung“ gilt auch hier: Ein Agent arbeitet mit Deinen Rechten. Virtuelle Maschine, Sicherungen, fragen, bevor Du etwas erlaubst. Du kennst das schon.

## Ein Cloud-Agent: Claude Code oder Codex

### Was Du brauchst

- **Ein Konto:** ein Claude-Abo oder einen API-Schlüssel von Anthropic für Claude Code, ein ChatGPT-Abo oder einen API-Schlüssel von OpenAI für Codex. Was der Agent tut, rechnen die ab, nicht Orcinus.
- **Node.js mit `npx`.** Orcinus startet den Agenten über seinen ACP-Adapter (`@agentclientprotocol/claude-agent-acp` bzw. `@agentclientprotocol/codex-acp`). Ist der Adapter nicht installiert, holt `npx` ihn beim ersten Start. Wer ihn lieber fest installiert: `npm install -g @agentclientprotocol/claude-agent-acp` (bzw. `…/codex-acp`).

### Schritt für Schritt

1. **Einmal im Terminal anmelden.**
   - Claude: `claude` starten und dort anmelden (installieren mit `npm install -g @anthropic-ai/claude-code` oder einmalig ausführen mit `npx -y @anthropic-ai/claude-code`).
   - Codex: `codex login` ausführen (installieren mit `npm install -g @openai/codex`). Benutzt Du stattdessen einen API-Schlüssel, trägst Du `OPENAI_API_KEY` oder `CODEX_API_KEY` im nächsten Schritt in die Umgebung des Agenten ein (*Bearbeiten …*).

   Der Agent in Orcinus benutzt diese Anmeldung. Dein Passwort sieht Orcinus nie.
2. **Agenten hinzufügen:** *Einstellungen → Orcinus einrichten … → KI-Unterstützung → Agenten → Hinzufügen … → Claude* (bzw. *Codex*). Der Eintrag zeigt den Befehl, den Orcinus startet. *Als Standard verwenden* macht ihn zum Agenten neuer Gespräche, *Nach oben* und *Nach unten* legen die Reihenfolge im Agentenmenü der Seitenleiste fest.
3. **Testen:** *Testen* startet den Agenten einmal und meldet seinen Namen und seine Version. Braucht er eine Anmeldung, steht im Ergebnis, wie das geht, und *Im Terminal anmelden …* öffnet ein Terminal mit dem richtigen Befehl.
4. **Losschreiben:** Öffne die Seitenleiste mit **F8** (*Ansicht → Seitenleisten → KI-Assistent*) und schreib eine Nachricht, zum Beispiel „Was liegt in diesem Ordner?“. Die erste Nachricht startet den Agenten in dem Ordner, den Du gerade offen hast.

Der Agent startet in seinem vorsichtigsten Modus: *Plan* bei Claude, *Read-only* bei Codex. Lockern kannst Du das im Feld *Modus* (siehe Kapitel „KI-Unterstützung“).

## Ein lokales Modell: LM Studio oder Strata

Ein lokales Modell läuft auf Deiner eigenen Grafikkarte. Nichts verlässt Deinen Rechner, und außer Strom kostet es nichts. Der Agent, der mit dem Modell arbeitet, ist OpenCode.

### Was Du brauchst

- **Einen Modellserver:**
  - LM Studio (`https://lmstudio.ai`) mit seinem Kommandozeilenwerkzeug `lms` und mindestens einem heruntergeladenen Modell, oder
  - Strata in `~/Strata` mit mindestens einer Modellkonfiguration.
- **OpenCode** (`https://opencode.ai`): `npm install -g opencode-ai`.

### Schritt für Schritt

1. *Einstellungen → Orcinus einrichten … → KI-Unterstützung → Agenten → Hinzufügen … → Lokales Modell (LM Studio) …* (bzw. *Lokales Modell (Strata) …*). Der Eintrag sagt Dir, wenn der Server oder seine Modelle nicht gefunden werden.
2. Wähle das Modell und die Kontextlänge (Orcinus schlägt eine vor, die zum Modell passt) und bestätige. Orcinus legt dann einen OpenCode-Agenten für dieses Modell an. OpenCodes eigene Konfiguration bleibt unverändert.
3. Schreib ihm in der Seitenleiste. Die erste Nachricht startet den Server und lädt das Modell. Das kann dauern (Strata: eine bis drei Minuten), ein guter Moment für einen Kaffee also. Die Seitenleiste zeigt den Fortschritt.

Ist LM Studio schon offen, startet und stoppt Orcinus nur dessen Server. Einen Strata-Server, den Du selbst gestartet hast, lässt Orcinus weiterlaufen.

## Andere Agenten

*Hinzufügen … → Anderer Agent …* nimmt jedes Programm, das das Agent Client Protocol über seine Standardein- und -ausgabe spricht, zum Beispiel Gemini CLI, Goose oder Qwen Code. Du gibst einen Namen, die Befehlszeile und, falls nötig, Umgebungsvariablen wie API-Schlüssel an. Wie sich der Agent anmeldet und welche Modi er anbietet, ist seine Sache.

## Wenn's nicht klappt

- **„… kann nicht gestartet werden“:** Der Befehl des Agenten wurde nicht gefunden. Installiere den Agenten oder seinen Adapter, oder prüfe, ob `npx` im Pfad der Sitzung liegt, in der Orcinus läuft.
- **„Der Agent braucht zuerst eine Anmeldung.“:** Melde Dich wie oben beschrieben an. *Testen* sagt Dir, wie.
- **Das lokale Modell startet nicht:** Die Seitenleiste zeigt den Grund (Server nicht gefunden, Modell fehlt, zu wenig Speicher). Die Einzelheiten stecken hinter der Meldung. LM Studio braucht sein Werkzeug `lms` im Pfad.
- **Der Agent fragt vor jedem Schritt:** Das ist sein Modus. Lockere ihn im Feld *Modus*.
- **Der Agent antwortet in einer anderen Sprache:** *KI-Unterstützung → Agenten → Sprache → Mit Agenten sprechen in*.
- **Mehr Einzelheiten:** Starte Orcinus aus einem Terminal mit `QT_LOGGING_RULES="io.github.arkarit.orcinus.ai.debug=true" orcinus`. Dann siehst Du, wie der Agent gestartet wird und was er in seine Fehlerausgabe schreibt.
