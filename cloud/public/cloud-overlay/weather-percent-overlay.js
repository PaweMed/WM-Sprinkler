(function weatherPercentOverlay() {
  const CARD_ID = "wms-cloud-weather-percent-card";
  const STYLE_ID = "wms-cloud-weather-percent-style";
  const LEGACY_ATTR = "data-wms-weather-legacy-hidden";
  const WEATHER_TITLE_VARIANTS = new Set(["pogoda", "weather", "wetter", "tiempo"]);
  const RAIN_STATS_MODAL_ID = "wms-cloud-rain-stats-modal";
  const RAIN_STATS_BUTTON_CLASS = "wms-rain-stats-open-btn";
  const RAIN_STATS_LABEL_VARIANTS = new Set([
    "opady deszczu 24h",
    "ostatnie 24h",
    "rainfall 24h",
    "last 24h",
    "niederschlag 24 std",
    "letzte 24 std",
    "lluvia 24 h",
    "ultimas 24 h",
  ]);
  const LEGACY_CARD_TITLE_VARIANTS = new Set([
    "automatyczny system nawadniania",
    "automatic irrigation system",
    "automatisches bewaesserungssystem",
    "sistema de riego automatico",
  ]);

  const labels = {
    title: {
      pl: "Aktualny mnoznik podlewania",
      en: "Current irrigation multiplier",
      de: "Aktueller Bewaesserungsfaktor",
      es: "Multiplicador actual de riego",
    },
    hint: {
      pl: "Realna decyzja silnika pogodowego na ten moment.",
      en: "Live weather-engine decision for this moment.",
      de: "Aktuelle Live-Entscheidung der Wetterlogik.",
      es: "Decision en vivo del motor meteorologico en este momento.",
    },
    autoModeDesc: {
      pl: "Automatyczne dostosowanie czasu podlewania na podstawie aktualnej pogody i wyliczonego mnoznika procentowego.",
      en: "Automatic watering-time adjustment based on live weather and the calculated percentage multiplier.",
      de: "Automatische Anpassung der Bewaesserungszeit anhand von Live-Wetterdaten und berechnetem Prozentfaktor.",
      es: "Ajuste automatico del tiempo de riego segun el clima en vivo y el multiplicador porcentual calculado.",
    },
    pushoverStatusReady: {
      pl: "Pushover: klucz uzytkownika i token sa zapisane (ukryte ze wzgledow bezpieczenstwa).",
      en: "Pushover: user key and API token are saved (hidden for security).",
      de: "Pushover: Benutzerschluessel und API-Token sind gespeichert (aus Sicherheitsgruenden ausgeblendet).",
      es: "Pushover: la clave de usuario y el token API estan guardados (ocultos por seguridad).",
    },
    pushoverStatusPartial: {
      pl: "Pushover: zapisano tylko czesc danych (brakuje user key lub tokenu).",
      en: "Pushover: only part of credentials is saved (missing user key or token).",
      de: "Pushover: nur ein Teil der Zugangsdaten ist gespeichert (Benutzerschluessel oder Token fehlt).",
      es: "Pushover: solo se guardo parte de las credenciales (falta la clave de usuario o el token).",
    },
    pushoverStatusMissing: {
      pl: "Pushover: brak zapisanych danych (wpisz user key i token, potem Zapisz ustawienia).",
      en: "Pushover: no credentials saved (enter user key and token, then Save settings).",
      de: "Pushover: keine Zugangsdaten gespeichert (Benutzerschluessel und Token eingeben, dann Einstellungen speichern).",
      es: "Pushover: no hay credenciales guardadas (ingresa clave de usuario y token, luego guarda ajustes).",
    },
    pushoverStatusDisabled: {
      pl: "Pushover jest wylaczony.",
      en: "Pushover is disabled.",
      de: "Pushover ist deaktiviert.",
      es: "Pushover esta desactivado.",
    },
    statusStop: {
      pl: "TRYB: HARD STOP",
      en: "MODE: HARD STOP",
      de: "MODUS: HARD STOP",
      es: "MODO: HARD STOP",
    },
    statusReduced: {
      pl: "TRYB: SKROCONE PODLEWANIE",
      en: "MODE: REDUCED WATERING",
      de: "MODUS: VERKUERZTE BEWAESSERUNG",
      es: "MODO: RIEGO REDUCIDO",
    },
    statusNormal: {
      pl: "TRYB: NORMALNE PODLEWANIE",
      en: "MODE: NORMAL WATERING",
      de: "MODUS: NORMALE BEWAESSERUNG",
      es: "MODO: RIEGO NORMAL",
    },
    statusExtended: {
      pl: "TRYB: WYDLUZONE PODLEWANIE",
      en: "MODE: EXTENDED WATERING",
      de: "MODUS: VERLAENGERTE BEWAESSERUNG",
      es: "MODO: RIEGO AMPLIADO",
    },
    statusNoData: {
      pl: "TRYB: BRAK DANYCH",
      en: "MODE: NO DATA",
      de: "MODUS: KEINE DATEN",
      es: "MODO: SIN DATOS",
    },
    loading: {
      pl: "Ladowanie danych pogodowych...",
      en: "Loading weather decision...",
      de: "Wetterentscheidung wird geladen...",
      es: "Cargando decision meteorologica...",
    },
    noData: {
      pl: "Brak danych /api/watering-percent.",
      en: "No /api/watering-percent data.",
      de: "Keine /api/watering-percent Daten.",
      es: "Sin datos de /api/watering-percent.",
    },
    tempTitle: {
      pl: "Temperatura: {value}C",
      en: "Temperature: {value}C",
      de: "Temperatur: {value}C",
      es: "Temperatura: {value}C",
    },
    rainTitle: {
      pl: "Opad 24h: {value} mm",
      en: "Rain 24h: {value} mm",
      de: "Regen 24h: {value} mm",
      es: "Lluvia 24h: {value} mm",
    },
    humTitle: {
      pl: "Wilgotnosc: {value}%",
      en: "Humidity: {value}%",
      de: "Luftfeuchte: {value}%",
      es: "Humedad: {value}%",
    },
    windTitle: {
      pl: "Wiatr: {value} km/h",
      en: "Wind: {value} km/h",
      de: "Wind: {value} km/h",
      es: "Viento: {value} km/h",
    },
    tempDetail: {
      pl: "Hard stop ponizej {threshold}C",
      en: "Hard stop below {threshold}C",
      de: "Hard stop unter {threshold}C",
      es: "Hard stop por debajo de {threshold}C",
    },
    tempDetailReduce: {
      pl: "Niska temperatura ({temp}C) skraca czas; hard stop dopiero < {threshold}C",
      en: "Low temperature ({temp}C) shortens time; hard stop only < {threshold}C",
      de: "Niedrige Temperatur ({temp}C) verkuerzt die Zeit; Hard-Stop erst < {threshold}C",
      es: "La temperatura baja ({temp}C) acorta el tiempo; hard stop solo < {threshold}C",
    },
    tempDetailBoost: {
      pl: "Wyzsza temperatura ({temp}C) wydluza czas podlewania",
      en: "Higher temperature ({temp}C) extends watering time",
      de: "Hoehere Temperatur ({temp}C) verlaengert die Bewaesserungszeit",
      es: "La temperatura mas alta ({temp}C) amplifica el tiempo de riego",
    },
    tempDetailNeutral: {
      pl: "Temperatura bez dodatkowej korekty; hard stop dopiero < {threshold}C",
      en: "Temperature without extra correction; hard stop only < {threshold}C",
      de: "Temperatur ohne Zusatzkorrektur; Hard-Stop erst < {threshold}C",
      es: "Temperatura sin correccion extra; hard stop solo < {threshold}C",
    },
    rainDetail: {
      pl: "Hard stop powyzej {threshold} mm / 24h",
      en: "Hard stop above {threshold} mm / 24h",
      de: "Hard stop ueber {threshold} mm / 24h",
      es: "Hard stop por encima de {threshold} mm / 24h",
    },
    rainDetailReduce: {
      pl: "Opad {rain} mm/24h skraca czas; hard stop dopiero > {threshold} mm",
      en: "Rain {rain} mm/24h shortens time; hard stop only > {threshold} mm",
      de: "Regen {rain} mm/24h verkuerzt Zeit; Hard-Stop erst > {threshold} mm",
      es: "Lluvia {rain} mm/24h reduce el tiempo; hard stop solo > {threshold} mm",
    },
    rainDetailNeutral: {
      pl: "Brak redukcji za opad; hard stop dopiero > {threshold} mm/24h",
      en: "No rain reduction; hard stop only > {threshold} mm/24h",
      de: "Keine Regenreduktion; Hard-Stop erst > {threshold} mm/24h",
      es: "Sin reduccion por lluvia; hard stop solo > {threshold} mm/24h",
    },
    humDetail: {
      pl: "Redukcja po przekroczeniu {threshold}%",
      en: "Reduction above {threshold}%",
      de: "Reduktion ueber {threshold}%",
      es: "Reduccion por encima de {threshold}%",
    },
    humDetailReduce: {
      pl: "Wilgotnosc {value}% przekroczyla prog {threshold}% i skrocila czas",
      en: "Humidity {value}% exceeded {threshold}% and reduced time",
      de: "Luftfeuchte {value}% ueberschritt {threshold}% und reduzierte Zeit",
      es: "La humedad {value}% supero {threshold}% y redujo el tiempo",
    },
    humDetailNeutral: {
      pl: "Wilgotnosc ponizej progu {threshold}%, bez dodatkowej redukcji",
      en: "Humidity below {threshold}%, no extra reduction",
      de: "Luftfeuchte unter {threshold}%, keine zusaetzliche Reduktion",
      es: "Humedad por debajo de {threshold}%, sin reduccion adicional",
    },
    windDetail: {
      pl: "Hard stop powyzej {threshold} km/h",
      en: "Hard stop above {threshold} km/h",
      de: "Hard stop ueber {threshold} km/h",
      es: "Hard stop por encima de {threshold} km/h",
    },
    windDetailNeutral: {
      pl: "Wiatr bez korekty; hard stop dopiero > {threshold} km/h",
      en: "Wind without correction; hard stop only > {threshold} km/h",
      de: "Wind ohne Korrektur; Hard-Stop erst > {threshold} km/h",
      es: "Viento sin correccion; hard stop solo > {threshold} km/h",
    },
    windDetailAdjust: {
      pl: "Mnoznik wiatru skorygowal czas; hard stop dopiero > {threshold} km/h",
      en: "Wind multiplier adjusted the time; hard stop only > {threshold} km/h",
      de: "Windfaktor hat die Zeit angepasst; Hard-Stop erst > {threshold} km/h",
      es: "El multiplicador de viento ajusto el tiempo; hard stop solo > {threshold} km/h",
    },
    hardStopTitle: {
      pl: "Powod zatrzymania",
      en: "Stop reason",
      de: "Stop-Grund",
      es: "Motivo de parada",
    },
    skippedTitle: {
      pl: "Pozostale czynniki pominiete",
      en: "Other factors skipped",
      de: "Weitere Faktoren uebersprungen",
      es: "Otros factores omitidos",
    },
    skippedDetail: {
      pl: "Po hard stop kolejne mnozniki nie sa liczone.",
      en: "After hard stop no further factors are applied.",
      de: "Nach Hard-Stop werden keine weiteren Faktoren berechnet.",
      es: "Tras hard stop no se aplican mas factores.",
    },
    clampTitle: {
      pl: "Limit procentu",
      en: "Percent limit",
      de: "Prozentgrenze",
      es: "Limite de porcentaje",
    },
    clampDetail: {
      pl: "Wynik obciety z {raw}% do zakresu {min}% - {max}%",
      en: "Result clamped from {raw}% to range {min}% - {max}%",
      de: "Ergebnis von {raw}% auf Bereich {min}% - {max}% begrenzt",
      es: "Resultado ajustado de {raw}% al rango {min}% - {max}%",
    },
    formula: {
      pl: "Wynik: 100% x {t} x {r} x {h} x {w} = {p}%",
      en: "Result: 100% x {t} x {r} x {h} x {w} = {p}%",
      de: "Ergebnis: 100% x {t} x {r} x {h} x {w} = {p}%",
      es: "Resultado: 100% x {t} x {r} x {h} x {w} = {p}%",
    },
    explainPrefix: {
      pl: "Silnik",
      en: "Engine",
      de: "Engine",
      es: "Motor",
    },
    hardStopTemp: {
      pl: "Temperatura ponizej progu: {temp}C < {limit}C",
      en: "Temperature below threshold: {temp}C < {limit}C",
      de: "Temperatur unter Grenzwert: {temp}C < {limit}C",
      es: "Temperatura por debajo del umbral: {temp}C < {limit}C",
    },
    hardStopWind: {
      pl: "Wiatr powyzej progu: {wind} km/h > {limit} km/h",
      en: "Wind above threshold: {wind} km/h > {limit} km/h",
      de: "Wind ueber Grenzwert: {wind} km/h > {limit} km/h",
      es: "Viento por encima del umbral: {wind} km/h > {limit} km/h",
    },
    hardStopRain: {
      pl: "Opad 24h powyzej progu: {rain} mm > {limit} mm",
      en: "Rain 24h above threshold: {rain} mm > {limit} mm",
      de: "Regen 24h ueber Grenzwert: {rain} mm > {limit} mm",
      es: "Lluvia 24h por encima del umbral: {rain} mm > {limit} mm",
    },
    hardStopUnknown: {
      pl: "Podlewanie zatrzymane przez warunek bezpieczenstwa.",
      en: "Watering stopped by a safety condition.",
      de: "Bewaesserung durch Sicherheitsbedingung gestoppt.",
      es: "Riego detenido por una condicion de seguridad.",
    },
    rainStatsOpen: {
      pl: "Historia opadow",
      en: "Rain history",
      de: "Regenhistorie",
      es: "Historial de lluvia",
    },
    rainStatsTitle: {
      pl: "Opady - historia i analiza",
      en: "Rainfall - history and analysis",
      de: "Niederschlag - Verlauf und Analyse",
      es: "Lluvia - historial y analisis",
    },
    rainStatsHint: {
      pl: "Dane sa zapisywane w Cloud per urzadzenie. Wybierz czestotliwosc i pobierz CSV.",
      en: "Data is stored in the Cloud per device. Select frequency and download CSV.",
      de: "Daten werden pro Geraet in der Cloud gespeichert. Frequenz waehlen und CSV herunterladen.",
      es: "Los datos se guardan en Cloud por dispositivo. Elige frecuencia y descarga CSV.",
    },
    rainStatsGrouping: {
      pl: "Czestotliwosc",
      en: "Frequency",
      de: "Hauefigkeit",
      es: "Frecuencia",
    },
    rainStatsGroupingDaily: {
      pl: "Dziennie",
      en: "Daily",
      de: "Taeglich",
      es: "Diario",
    },
    rainStatsGroupingWeekly: {
      pl: "Tygodniowo",
      en: "Weekly",
      de: "Woechentlich",
      es: "Semanal",
    },
    rainStatsGroupingMonthly: {
      pl: "Miesiecznie",
      en: "Monthly",
      de: "Monatlich",
      es: "Mensual",
    },
    rainStatsGroupingYearly: {
      pl: "Rocznie",
      en: "Yearly",
      de: "Jaehrlich",
      es: "Anual",
    },
    rainStatsRefresh: {
      pl: "Odswiez",
      en: "Refresh",
      de: "Aktualisieren",
      es: "Actualizar",
    },
    rainStatsDownload: {
      pl: "Pobierz CSV",
      en: "Download CSV",
      de: "CSV herunterladen",
      es: "Descargar CSV",
    },
    rainStatsLoading: {
      pl: "Ladowanie statystyk opadow...",
      en: "Loading rainfall stats...",
      de: "Lade Niederschlagsstatistik...",
      es: "Cargando estadisticas de lluvia...",
    },
    rainStatsNoData: {
      pl: "Brak danych opadowych dla wybranego zakresu.",
      en: "No rainfall data for the selected range.",
      de: "Keine Niederschlagsdaten fuer den gewaehlten Bereich.",
      es: "Sin datos de lluvia para el rango seleccionado.",
    },
    rainStatsLegend: {
      pl: "Suma: {total} mm | Max slupek: {max} mm | Punkty: {points}",
      en: "Total: {total} mm | Max bar: {max} mm | Points: {points}",
      de: "Summe: {total} mm | Max Balken: {max} mm | Punkte: {points}",
      es: "Total: {total} mm | Barra max: {max} mm | Puntos: {points}",
    },
  };

  let percentData = null;
  let percentLoading = false;
  let percentError = "";
  let lastFetchTs = 0;
  let settingsLoading = false;
  let settingsData = null;
  let lastSettingsFetchTs = 0;
  let refreshQueued = false;
  let forceFetchQueued = false;
  let observerPaused = false;
  let domObserver = null;
  let rainStatsLoading = false;
  let rainStatsPayload = null;
  let rainStatsError = "";
  let rainStatsGrouping = "daily";
  let rainStatsModalBound = false;
  let rainStatsLastPointerActionTs = 0;
  const observerConfig = { childList: true, subtree: true };

  function normalizeText(value) {
    return String(value || "")
      .trim()
      .toLowerCase()
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .replace(/[^\p{L}\p{N}]+/gu, " ")
      .replace(/\s+/g, " ")
      .trim();
  }

  function currentLang() {
    const raw = String(document.documentElement.lang || "pl").toLowerCase();
    const base = raw.split("-")[0];
    return ["pl", "en", "de", "es"].includes(base) ? base : "pl";
  }

  function tr(key, vars) {
    const dict = labels[key] || {};
    const template = dict[currentLang()] || dict.en || dict.pl || key;
    if (!vars) return template;
    return String(template).replace(/\{(\w+)\}/g, (_, name) => (
      Object.prototype.hasOwnProperty.call(vars, name) ? String(vars[name]) : ""
    ));
  }

  function ensureStyle() {
    if (document.getElementById(STYLE_ID)) return;
    const style = document.createElement("style");
    style.id = STYLE_ID;
    style.textContent = `
      .wms-weather-legacy-hidden { display: none !important; }
      #${CARD_ID} {
        position: relative;
        overflow: hidden;
        border: 1px solid rgba(148, 163, 184, 0.30);
        border-radius: 20px;
      }
      #${CARD_ID}::before {
        content: "";
        position: absolute;
        inset: 0;
        background:
          radial-gradient(550px 230px at -10% -30%, rgba(59,130,246,0.12), transparent 60%),
          radial-gradient(450px 240px at 110% 110%, rgba(34,197,94,0.12), transparent 60%);
        pointer-events: none;
      }
      .wms-weather-pct-shell {
        position: relative;
        z-index: 1;
      }
      .wms-weather-pct-head {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
      }
      .wms-weather-pct-title {
        margin: 0;
        font-size: 16px;
        font-weight: 700;
        color: #0f172a;
      }
      .wms-weather-pct-hint {
        margin: 4px 0 0;
        font-size: 12px;
        color: #64748b;
      }
      .wms-weather-pct-status {
        border-radius: 999px;
        border: 1px solid #cbd5e1;
        background: #f8fafc;
        color: #334155;
        font-size: 11px;
        font-weight: 800;
        letter-spacing: 0.02em;
        padding: 5px 10px;
        white-space: nowrap;
      }
      .wms-weather-pct-status.stop { background: #fee2e2; border-color: #fca5a5; color: #991b1b; }
      .wms-weather-pct-status.reduced { background: #ffedd5; border-color: #fdba74; color: #9a3412; }
      .wms-weather-pct-status.normal { background: #dcfce7; border-color: #86efac; color: #166534; }
      .wms-weather-pct-status.extended { background: #dbeafe; border-color: #93c5fd; color: #1d4ed8; }
      .wms-weather-pct-status.nodata { background: #f1f5f9; border-color: #cbd5e1; color: #475569; }
      .wms-weather-pct-dial-wrap {
        margin-top: 12px;
        display: grid;
        place-items: center;
        position: relative;
      }
      .wms-weather-pct-dial {
        --wms-dial-value: 0;
        --wms-dial-color: #16a34a;
        width: 212px;
        height: 212px;
        border-radius: 999px;
        padding: 13px;
        background:
          radial-gradient(circle at 34% 22%, rgba(255,255,255,0.90), rgba(255,255,255,0.14) 42%, transparent 58%),
          conic-gradient(var(--wms-dial-color) calc(var(--wms-dial-value) * 1%), rgba(148, 163, 184, 0.36) 0);
        box-shadow:
          inset 0 0 0 1px rgba(255,255,255,0.85),
          inset 0 8px 24px rgba(255,255,255,0.35),
          0 14px 24px rgba(15, 23, 42, 0.18);
      }
      .wms-weather-pct-dial::before {
        content: "";
        display: block;
        width: 100%;
        height: 100%;
        border-radius: inherit;
        background:
          radial-gradient(circle at 28% 18%, rgba(255,255,255,0.44), rgba(255,255,255,0.03) 58%),
          linear-gradient(180deg, rgba(255,255,255,0.34), rgba(255,255,255,0.05) 40%),
          linear-gradient(180deg, var(--wms-dial-color), var(--wms-dial-color));
        box-shadow: inset 0 -18px 22px rgba(0,0,0,0.16);
      }
      .wms-weather-pct-center {
        position: absolute;
        inset: 0;
        pointer-events: none;
      }
      .wms-weather-pct-value {
        position: absolute;
        left: 50%;
        top: 50%;
        transform: translate(-50%, -50%);
        font-size: 52px;
        font-weight: 900;
        line-height: 1;
        color: #fff;
        text-shadow: 0 2px 10px rgba(0,0,0,0.30);
        text-align: center;
        white-space: nowrap;
      }
      .wms-weather-pct-reasons {
        margin-top: 12px;
        display: grid;
        gap: 8px;
      }
      .wms-weather-pct-reason {
        display: flex;
        align-items: baseline;
        justify-content: space-between;
        gap: 10px;
        border: 1px solid #e2e8f0;
        border-radius: 12px;
        background: #f8fafc;
        padding: 9px 11px;
      }
      .wms-weather-pct-reason-main {
        min-width: 0;
        color: #0f172a;
        font-size: 12px;
        font-weight: 700;
      }
      .wms-weather-pct-reason-detail {
        margin-top: 2px;
        color: #64748b;
        font-size: 11px;
        font-weight: 500;
      }
      .wms-weather-pct-delta {
        min-width: 52px;
        text-align: center;
        border-radius: 999px;
        padding: 4px 8px;
        font-size: 11px;
        font-weight: 800;
        border: 1px solid #e2e8f0;
        color: #334155;
        background: #fff;
      }
      .wms-weather-pct-delta.down { color: #9a3412; background: #ffedd5; border-color: #fdba74; }
      .wms-weather-pct-delta.up { color: #0f766e; background: #ccfbf1; border-color: #5eead4; }
      .wms-weather-pct-delta.stop { color: #991b1b; background: #fee2e2; border-color: #fca5a5; }
      .wms-weather-pct-foot {
        margin-top: 10px;
        color: #64748b;
        font-size: 11px;
        line-height: 1.45;
      }
      .wms-weather-pct-foot strong {
        color: #0f172a;
      }
      .wms-weather-pct-explain {
        margin-top: 6px;
      }
      .${RAIN_STATS_BUTTON_CLASS} {
        margin-left: 8px;
        border: 1px solid #cbd5e1;
        background: #eff6ff;
        color: #1d4ed8;
        border-radius: 999px;
        width: 24px;
        height: 24px;
        font-size: 14px;
        line-height: 1;
        display: inline-grid;
        place-items: center;
        cursor: pointer;
        vertical-align: middle;
      }
      .${RAIN_STATS_BUTTON_CLASS}:hover {
        background: #dbeafe;
      }
      .wms-rain-modal {
        position: fixed;
        inset: 0;
        z-index: 2147483300;
        background: rgba(15, 23, 42, 0.45);
        display: none;
        align-items: center;
        justify-content: center;
        padding: 18px;
      }
      .wms-rain-modal.show {
        display: flex;
      }
      .wms-rain-modal-card {
        width: min(960px, 100%);
        max-height: 88vh;
        overflow: auto;
        border-radius: 16px;
        border: 1px solid #cbd5e1;
        background: #ffffff;
        padding: 16px;
        box-shadow: 0 24px 48px rgba(15, 23, 42, 0.28);
      }
      .wms-rain-modal-head {
        display: flex;
        justify-content: space-between;
        align-items: center;
        gap: 10px;
      }
      .wms-rain-modal-title {
        margin: 0;
        color: #0f172a;
        font-size: 17px;
        font-weight: 800;
      }
      .wms-rain-modal-close {
        border: 1px solid #cbd5e1;
        border-radius: 999px;
        width: 30px;
        height: 30px;
        background: #f8fafc;
        color: #334155;
        cursor: pointer;
      }
      .wms-rain-modal-hint {
        margin: 8px 0 0;
        font-size: 12px;
        color: #64748b;
      }
      .wms-rain-controls {
        margin-top: 12px;
        display: flex;
        flex-wrap: wrap;
        gap: 8px;
        align-items: end;
      }
      .wms-rain-controls label {
        display: grid;
        gap: 6px;
        font-size: 12px;
        color: #334155;
        font-weight: 700;
      }
      .wms-rain-controls select,
      .wms-rain-controls button {
        border: 1px solid #cbd5e1;
        border-radius: 10px;
        background: #fff;
        color: #0f172a;
        font-size: 12px;
        padding: 8px 10px;
      }
      .wms-rain-controls button {
        cursor: pointer;
        font-weight: 700;
      }
      .wms-rain-chart-wrap {
        margin-top: 12px;
        border: 1px solid #e2e8f0;
        border-radius: 12px;
        background: #f8fafc;
        padding: 10px;
      }
      .wms-rain-chart {
        display: flex;
        gap: 6px;
        align-items: end;
        min-height: 230px;
        overflow-x: auto;
        padding-bottom: 6px;
      }
      .wms-rain-col {
        min-width: 22px;
        flex: 0 0 22px;
        display: grid;
        gap: 6px;
        align-items: end;
      }
      .wms-rain-col-bar {
        border-radius: 8px 8px 4px 4px;
        background: linear-gradient(180deg, #3b82f6, #1d4ed8);
        min-height: 2px;
      }
      .wms-rain-col-label {
        font-size: 10px;
        color: #64748b;
        text-align: center;
        min-height: 12px;
      }
      .wms-rain-col-value {
        font-size: 10px;
        color: #334155;
        text-align: center;
        min-height: 12px;
      }
      .wms-rain-legend {
        margin-top: 8px;
        font-size: 12px;
        color: #334155;
      }
      @media (max-width: 560px) {
        .wms-weather-pct-dial {
          width: 188px;
          height: 188px;
        }
        .wms-weather-pct-value {
          font-size: 46px;
        }
        .wms-rain-modal-card {
          padding: 12px;
        }
        .wms-rain-chart {
          min-height: 190px;
        }
      }
    `;
    document.head.appendChild(style);
  }

  function patchAutoModeDescription() {
    const root = document.getElementById("root");
    if (!root) return;
    const cards = Array.from(root.querySelectorAll(".ios-card"));
    for (const card of cards) {
      if (!(card instanceof HTMLElement) || card.offsetParent === null) continue;
      const heading = card.querySelector("h3");
      if (!(heading instanceof HTMLElement)) continue;
      const headingText = normalizeText(heading.textContent || "");
      if (!["automatyka", "automation", "automatisierung", "automatizacion"].includes(headingText)) continue;

      const labelsInCard = Array.from(card.querySelectorAll("label"));
      const autoLabel = labelsInCard.find((node) => {
        if (!(node instanceof HTMLElement)) return false;
        const txt = normalizeText(node.textContent || "");
        return [
          "tryb automatyczny",
          "automatic mode",
          "automatikmodus",
          "modo automatico",
        ].includes(txt);
      });
      if (!(autoLabel instanceof HTMLElement)) continue;

      const labelBlock = autoLabel.parentElement;
      const desc = labelBlock ? labelBlock.querySelector("p") : null;
      if (!(desc instanceof HTMLElement)) continue;
      desc.textContent = tr("autoModeDesc");
      return;
    }
  }

  function patchSecretPlaceholders() {
    const root = document.getElementById("root");
    if (!root) return;
    const cards = Array.from(root.querySelectorAll(".ios-card"));
    const MASK = "••••••••••••••••";

    const setMaskedPlaceholder = (inputEl, enabled) => {
      if (!(inputEl instanceof HTMLInputElement)) return;
      if (!Object.prototype.hasOwnProperty.call(inputEl.dataset, "wmsOrigPlaceholder")) {
        inputEl.dataset.wmsOrigPlaceholder = inputEl.getAttribute("placeholder") || "";
      }
      const basePlaceholder = inputEl.dataset.wmsOrigPlaceholder || "";
      if (inputEl.value && inputEl.value.length > 0) return;
      inputEl.setAttribute("placeholder", enabled ? MASK : basePlaceholder);
    };

    const findInputByLabel = (card, acceptedLabels) => {
      const labelsInCard = Array.from(card.querySelectorAll("label"));
      const labelNode = labelsInCard.find((node) => (
        node instanceof HTMLElement
        && acceptedLabels.includes(normalizeText(node.textContent || ""))
      ));
      if (!(labelNode instanceof HTMLElement)) return null;
      const block = labelNode.parentElement || labelNode;
      const withinBlock = block.querySelector("input");
      if (withinBlock instanceof HTMLInputElement) return withinBlock;
      let sibling = labelNode.nextElementSibling;
      while (sibling) {
        if (sibling instanceof HTMLInputElement) return sibling;
        if (sibling instanceof HTMLElement) {
          const nested = sibling.querySelector("input");
          if (nested instanceof HTMLInputElement) return nested;
        }
        sibling = sibling.nextElementSibling;
      }
      return null;
    };

    const hasOwm = !!(settingsData && settingsData.owmApiKeyConfigured);
    const hasPushoverUser = !!(settingsData && settingsData.pushoverUserConfigured);
    const hasPushoverToken = !!(settingsData && settingsData.pushoverTokenConfigured);

    for (const card of cards) {
      if (!(card instanceof HTMLElement) || card.offsetParent === null) continue;
      const heading = card.querySelector("h3");
      if (!(heading instanceof HTMLElement)) continue;
      const headingText = normalizeText(heading.textContent || "");

      if ([
        "powiadomienia pushover",
        "pushover notifications",
        "pushover benachrichtigungen",
        "notificaciones de pushover",
      ].includes(headingText)) {
        const userInput = findInputByLabel(card, [
          "user key",
          "klucz uzytkownika pushover",
          "pushover benutzerschluessel",
          "clave de usuario de pushover",
        ]);
        const tokenInput = findInputByLabel(card, [
          "api token",
          "token api aplikacji",
          "api token",
          "token api de la aplicacion",
        ]);
        setMaskedPlaceholder(userInput, hasPushoverUser);
        setMaskedPlaceholder(tokenInput, hasPushoverToken);
        continue;
      }

      if ([
        "openweathermap api",
      ].includes(headingText)) {
        const owmInput = findInputByLabel(card, [
          "klucz api openweathermap",
          "openweathermap api key",
          "openweathermap api schluessel",
          "clave api de openweathermap",
        ]);
        setMaskedPlaceholder(owmInput, hasOwm);
      }
    }
  }

  function finiteNumber(value, fallback) {
    const num = Number(value);
    return Number.isFinite(num) ? num : fallback;
  }

  function fmt(value, digits, fallback) {
    if (!Number.isFinite(value)) return fallback || "-";
    return Number(value).toFixed(digits);
  }

  function fmtFactor(value) {
    return Number.isFinite(value) ? `x${Number(value).toFixed(2)}` : "-";
  }

  function fmtRain(value, digits = 1, fallback = "0.0") {
    const num = Number(value);
    if (!Number.isFinite(num)) return fallback;
    return num.toFixed(digits);
  }

  function rainGroupingLabel(grouping) {
    if (grouping === "weekly") return tr("rainStatsGroupingWeekly");
    if (grouping === "monthly") return tr("rainStatsGroupingMonthly");
    if (grouping === "yearly") return tr("rainStatsGroupingYearly");
    return tr("rainStatsGroupingDaily");
  }

  function findRain24hLabelNode(section) {
    if (!(section instanceof HTMLElement)) return null;
    const candidates = Array.from(section.querySelectorAll("div, span, p, strong, small, h4"));
    return candidates.find((node) => {
      if (!(node instanceof HTMLElement)) return false;
      if (node.offsetParent === null) return false;
      const txt = normalizeText(node.textContent || "");
      return RAIN_STATS_LABEL_VARIANTS.has(txt);
    }) || null;
  }

  function ensureRainStatsModal() {
    let modal = document.getElementById(RAIN_STATS_MODAL_ID);
    if (!(modal instanceof HTMLElement)) {
      modal = document.createElement("div");
      modal.id = RAIN_STATS_MODAL_ID;
      modal.className = "wms-rain-modal";
      modal.innerHTML = `
        <div class="wms-rain-modal-card" role="dialog" aria-modal="true" aria-labelledby="${RAIN_STATS_MODAL_ID}-title">
          <div class="wms-rain-modal-head">
            <h3 class="wms-rain-modal-title" id="${RAIN_STATS_MODAL_ID}-title"></h3>
            <button type="button" class="wms-rain-modal-close" id="${RAIN_STATS_MODAL_ID}-close">×</button>
          </div>
          <p class="wms-rain-modal-hint" id="${RAIN_STATS_MODAL_ID}-hint"></p>
          <div class="wms-rain-controls">
            <label>
              <span id="${RAIN_STATS_MODAL_ID}-group-label"></span>
              <select id="${RAIN_STATS_MODAL_ID}-group">
                <option value="daily"></option>
                <option value="weekly"></option>
                <option value="monthly"></option>
                <option value="yearly"></option>
              </select>
            </label>
            <button type="button" id="${RAIN_STATS_MODAL_ID}-refresh"></button>
            <button type="button" id="${RAIN_STATS_MODAL_ID}-download"></button>
          </div>
          <div class="wms-rain-chart-wrap">
            <div class="wms-rain-chart" id="${RAIN_STATS_MODAL_ID}-chart"></div>
            <div class="wms-rain-legend" id="${RAIN_STATS_MODAL_ID}-legend"></div>
          </div>
        </div>
      `;
      document.body.appendChild(modal);
    }

    const title = document.getElementById(`${RAIN_STATS_MODAL_ID}-title`);
    if (title) title.textContent = tr("rainStatsTitle");
    const hint = document.getElementById(`${RAIN_STATS_MODAL_ID}-hint`);
    if (hint) hint.textContent = tr("rainStatsHint");
    const groupLabel = document.getElementById(`${RAIN_STATS_MODAL_ID}-group-label`);
    if (groupLabel) groupLabel.textContent = tr("rainStatsGrouping");
    const groupSelect = document.getElementById(`${RAIN_STATS_MODAL_ID}-group`);
    if (groupSelect instanceof HTMLSelectElement) {
      const opts = Array.from(groupSelect.options);
      opts.forEach((opt) => {
        if (!(opt instanceof HTMLOptionElement)) return;
        if (opt.value === "weekly") opt.textContent = tr("rainStatsGroupingWeekly");
        else if (opt.value === "monthly") opt.textContent = tr("rainStatsGroupingMonthly");
        else if (opt.value === "yearly") opt.textContent = tr("rainStatsGroupingYearly");
        else opt.textContent = tr("rainStatsGroupingDaily");
      });
      groupSelect.value = rainStatsGrouping;
    }
    const refreshBtn = document.getElementById(`${RAIN_STATS_MODAL_ID}-refresh`);
    if (refreshBtn) refreshBtn.textContent = tr("rainStatsRefresh");
    const downloadBtn = document.getElementById(`${RAIN_STATS_MODAL_ID}-download`);
    if (downloadBtn) downloadBtn.textContent = tr("rainStatsDownload");

    if (!rainStatsModalBound) {
      rainStatsModalBound = true;
      modal.addEventListener("pointerup", (event) => {
        const target = event.target;
        if (!(target instanceof Element)) return;
        if (target === modal || target.closest(`#${RAIN_STATS_MODAL_ID}-close`)) {
          rainStatsLastPointerActionTs = Date.now();
          modal.classList.remove("show");
          event.preventDefault();
          event.stopPropagation();
        }
      }, { capture: true });
      modal.addEventListener("click", (event) => {
        if (Date.now() - rainStatsLastPointerActionTs < 350) return;
        const target = event.target;
        if (!(target instanceof Element)) return;
        if (target === modal || target.closest(`#${RAIN_STATS_MODAL_ID}-close`)) {
          modal.classList.remove("show");
        }
      }, { capture: true });

      if (groupSelect instanceof HTMLSelectElement) {
        groupSelect.addEventListener("change", () => {
          const selected = String(groupSelect.value || "daily").toLowerCase();
          rainStatsGrouping = ["daily", "weekly", "monthly", "yearly"].includes(selected) ? selected : "daily";
          fetchRainStats(true);
        });
      }
      if (refreshBtn instanceof HTMLButtonElement) {
        refreshBtn.addEventListener("click", () => fetchRainStats(true));
      }
      if (downloadBtn instanceof HTMLButtonElement) {
        downloadBtn.addEventListener("click", async () => {
          const grouping = ["daily", "weekly", "monthly", "yearly"].includes(rainStatsGrouping) ? rainStatsGrouping : "daily";
          try {
            const resp = await fetch(`/api/rain/stats/export?grouping=${encodeURIComponent(grouping)}`, {
              credentials: "same-origin",
              cache: "no-store",
            });
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            const blob = await resp.blob();
            const cd = String(resp.headers.get("content-disposition") || "");
            const nameMatch = /filename=\"?([^\";]+)\"?/i.exec(cd);
            const fileName = nameMatch ? nameMatch[1] : `rain-stats_${grouping}.csv`;
            const url = URL.createObjectURL(blob);
            const a = document.createElement("a");
            a.href = url;
            a.download = fileName;
            document.body.appendChild(a);
            a.click();
            a.remove();
            URL.revokeObjectURL(url);
          } catch {
            // Keep UX simple: failed export does not break modal flow.
          }
        });
      }
    }
    return modal;
  }

  function renderRainStatsModal() {
    const chart = document.getElementById(`${RAIN_STATS_MODAL_ID}-chart`);
    const legend = document.getElementById(`${RAIN_STATS_MODAL_ID}-legend`);
    if (!(chart instanceof HTMLElement) || !(legend instanceof HTMLElement)) return;
    chart.textContent = "";
    legend.textContent = "";

    if (rainStatsLoading) {
      legend.textContent = tr("rainStatsLoading");
      return;
    }
    if (rainStatsError) {
      legend.textContent = rainStatsError;
      return;
    }
    const series = Array.isArray(rainStatsPayload?.series) ? rainStatsPayload.series : [];
    if (!series.length) {
      legend.textContent = tr("rainStatsNoData");
      return;
    }

    const maxValue = Math.max(0.1, ...series.map((item) => Math.max(0, Number(item?.rainMm || 0))));
    const labelStep = Math.max(1, Math.ceil(series.length / 12));
    const showValues = series.length <= 45;

    series.forEach((point, idx) => {
      const value = Math.max(0, Number(point?.rainMm || 0));
      const col = document.createElement("div");
      col.className = "wms-rain-col";
      col.title = `${String(point?.fullLabel || point?.key || "-")}: ${fmtRain(value, 2, "0.00")} mm`;

      const valueEl = document.createElement("div");
      valueEl.className = "wms-rain-col-value";
      valueEl.textContent = (showValues && value > 0) ? fmtRain(value, 1, "0.0") : "";

      const bar = document.createElement("div");
      bar.className = "wms-rain-col-bar";
      bar.style.height = `${Math.max(2, Math.round((value / maxValue) * 186))}px`;

      const label = document.createElement("div");
      label.className = "wms-rain-col-label";
      label.textContent = (idx % labelStep === 0 || idx === series.length - 1) ? String(point?.label || "") : "";

      col.appendChild(valueEl);
      col.appendChild(bar);
      col.appendChild(label);
      chart.appendChild(col);
    });

    legend.textContent = tr("rainStatsLegend", {
      total: fmtRain(rainStatsPayload?.totalMm, 1, "0.0"),
      max: fmtRain(rainStatsPayload?.maxBucketMm, 1, "0.0"),
      points: Number(rainStatsPayload?.points || series.length),
      mode: rainGroupingLabel(rainStatsGrouping),
    });
  }

  async function fetchRainStats(force = false) {
    if (rainStatsLoading) return;
    const modal = document.getElementById(RAIN_STATS_MODAL_ID);
    if (!(modal instanceof HTMLElement) || !modal.classList.contains("show")) return;
    const grouping = ["daily", "weekly", "monthly", "yearly"].includes(rainStatsGrouping) ? rainStatsGrouping : "daily";
    rainStatsLoading = true;
    renderRainStatsModal();
    try {
      const resp = await fetch(`/api/rain/stats?grouping=${encodeURIComponent(grouping)}${force ? "&force=1" : ""}`, {
        credentials: "same-origin",
        cache: "no-store",
      });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json().catch(() => null);
      if (!data || typeof data !== "object" || data.ok !== true) {
        throw new Error(tr("rainStatsNoData"));
      }
      rainStatsPayload = data;
      rainStatsError = "";
    } catch (err) {
      rainStatsError = String(err?.message || err || tr("rainStatsNoData"));
      rainStatsPayload = null;
    } finally {
      rainStatsLoading = false;
      renderRainStatsModal();
    }
  }

  function openRainStatsModal() {
    const modal = ensureRainStatsModal();
    modal.classList.add("show");
    renderRainStatsModal();
    fetchRainStats(true);
  }

  function wireRainStatsTrigger(section) {
    const label = findRain24hLabelNode(section);
    if (!(label instanceof HTMLElement)) return;
    const host = label.parentElement || label;
    let button = host.querySelector(`.${RAIN_STATS_BUTTON_CLASS}`);
    if (!(button instanceof HTMLButtonElement)) {
      button = document.createElement("button");
      button.type = "button";
      button.className = RAIN_STATS_BUTTON_CLASS;
      button.setAttribute("title", tr("rainStatsOpen"));
      button.setAttribute("aria-label", tr("rainStatsOpen"));
      button.textContent = "i";
      host.appendChild(button);
    }
    button.onclick = (event) => {
      event.preventDefault();
      event.stopPropagation();
      openRainStatsModal();
    };
  }

  function getWeatherContainer() {
    const root = document.getElementById("root");
    if (!root) return null;
    const title = Array.from(root.querySelectorAll("h2")).find((node) => {
      if (!(node instanceof HTMLElement)) return false;
      if (node.offsetParent === null) return false;
      return WEATHER_TITLE_VARIANTS.has(normalizeText(node.textContent || ""));
    });
    if (!title) return null;
    return title.closest(".px-4.py-6, .px-4") || title.parentElement?.parentElement || null;
  }

  function revealLegacyCards() {
    document.querySelectorAll(`.ios-card[${LEGACY_ATTR}='1']`).forEach((card) => {
      if (!(card instanceof HTMLElement)) return;
      card.style.display = "";
      card.removeAttribute(LEGACY_ATTR);
      card.classList.remove("wms-weather-legacy-hidden");
    });
  }

  function findLegacyWeatherCard(container) {
    if (!(container instanceof HTMLElement)) return null;
    const byTitle = Array.from(container.querySelectorAll(".ios-card h3")).find((titleNode) => {
      if (!(titleNode instanceof HTMLElement)) return false;
      const normalized = normalizeText(titleNode.textContent || "");
      return LEGACY_CARD_TITLE_VARIANTS.has(normalized);
    });
    if (byTitle) return byTitle.closest(".ios-card");

    const byGradientRow = container.querySelector(".ios-card .space-y-2.text-xs div[style*='linear-gradient']");
    if (byGradientRow instanceof HTMLElement) return byGradientRow.closest(".ios-card");
    return null;
  }

  function ensureCard(container, anchor) {
    let card = document.getElementById(CARD_ID);
    if (!(card instanceof HTMLElement)) {
      card = document.createElement("div");
      card.id = CARD_ID;
      card.className = "ios-card p-4 mb-4";
      card.setAttribute("data-no-translate", "1");
      card.innerHTML = `
        <div class="wms-weather-pct-shell">
          <div class="wms-weather-pct-head">
            <div>
              <h3 class="wms-weather-pct-title" id="${CARD_ID}-title"></h3>
              <p class="wms-weather-pct-hint" id="${CARD_ID}-hint"></p>
            </div>
            <span class="wms-weather-pct-status nodata" id="${CARD_ID}-status"></span>
          </div>
          <div class="wms-weather-pct-dial-wrap">
            <div class="wms-weather-pct-dial" id="${CARD_ID}-dial"></div>
            <div class="wms-weather-pct-center">
              <div class="wms-weather-pct-value" id="${CARD_ID}-value">--%</div>
            </div>
          </div>
          <div class="wms-weather-pct-reasons" id="${CARD_ID}-reasons"></div>
          <div class="wms-weather-pct-foot" id="${CARD_ID}-foot"></div>
        </div>
      `;
    }

    if (anchor instanceof HTMLElement && anchor.parentElement === container) {
      if (card.previousElementSibling !== anchor || card.parentElement !== container) {
        anchor.insertAdjacentElement("afterend", card);
      }
    } else if (card.parentElement !== container) {
      container.appendChild(card);
    }
    return card;
  }

  function hardStopDescription(data) {
    const reasonCode = String(data?.hard_stop_reason_code || data?.hard_stop_reason || "").toLowerCase();
    const temp = finiteNumber(data?.temp_now, NaN);
    const tempLimit = finiteNumber(data?.threshold_temp_skip_c, NaN);
    const wind = finiteNumber(data?.wind_now_kmh, NaN);
    const windLimit = finiteNumber(data?.threshold_wind_skip_kmh, NaN);
    const rain = finiteNumber(data?.rain_24h, NaN);
    const rainLimit = finiteNumber(data?.threshold_rain_skip_mm, NaN);

    if (reasonCode.includes("temp")) {
      return tr("hardStopTemp", { temp: fmt(temp, 1, "-"), limit: fmt(tempLimit, 1, "-") });
    }
    if (reasonCode.includes("wind")) {
      return tr("hardStopWind", { wind: fmt(wind, 1, "-"), limit: fmt(windLimit, 1, "-") });
    }
    if (reasonCode.includes("rain")) {
      return tr("hardStopRain", { rain: fmt(rain, 1, "-"), limit: fmt(rainLimit, 1, "-") });
    }
    return tr("hardStopUnknown");
  }

  function appendReason(host, titleText, detailText, deltaText, toneClass) {
    if (!(host instanceof HTMLElement)) return;
    const row = document.createElement("div");
    row.className = "wms-weather-pct-reason";

    const left = document.createElement("div");
    left.className = "wms-weather-pct-reason-main";
    left.textContent = String(titleText || "-");
    const detail = document.createElement("div");
    detail.className = "wms-weather-pct-reason-detail";
    detail.textContent = String(detailText || "");
    left.appendChild(detail);

    const delta = document.createElement("div");
    delta.className = `wms-weather-pct-delta ${toneClass || ""}`.trim();
    delta.textContent = String(deltaText || "-");

    row.appendChild(left);
    row.appendChild(delta);
    host.appendChild(row);
  }

  function renderCard(card) {
    if (!(card instanceof HTMLElement)) return;
    const titleEl = document.getElementById(`${CARD_ID}-title`);
    const hintEl = document.getElementById(`${CARD_ID}-hint`);
    const statusEl = document.getElementById(`${CARD_ID}-status`);
    const dialEl = document.getElementById(`${CARD_ID}-dial`);
    const valueEl = document.getElementById(`${CARD_ID}-value`);
    const reasonsEl = document.getElementById(`${CARD_ID}-reasons`);
    const footEl = document.getElementById(`${CARD_ID}-foot`);

    if (titleEl) titleEl.textContent = tr("title");
    if (hintEl) hintEl.textContent = tr("hint");
    if (reasonsEl) reasonsEl.textContent = "";

    if (percentLoading && !percentData) {
      if (statusEl) {
        statusEl.className = "wms-weather-pct-status nodata";
        statusEl.textContent = tr("statusNoData");
      }
      if (valueEl) valueEl.textContent = "--%";
      if (dialEl) {
        dialEl.style.setProperty("--wms-dial-value", "0");
        dialEl.style.setProperty("--wms-dial-color", "#64748b");
      }
      if (reasonsEl) appendReason(reasonsEl, tr("loading"), tr("loading"), "-", "");
      if (footEl) footEl.textContent = "";
      return;
    }

    if (!percentData || typeof percentData !== "object") {
      if (statusEl) {
        statusEl.className = "wms-weather-pct-status nodata";
        statusEl.textContent = tr("statusNoData");
      }
      if (valueEl) valueEl.textContent = "--%";
      if (dialEl) {
        dialEl.style.setProperty("--wms-dial-value", "0");
        dialEl.style.setProperty("--wms-dial-color", "#64748b");
      }
      const msg = percentError ? `${tr("noData")} ${percentError}` : tr("noData");
      if (reasonsEl) appendReason(reasonsEl, tr("noData"), msg, "-", "");
      if (footEl) footEl.textContent = "";
      return;
    }

    const data = percentData;
    const percent = Math.max(0, Math.round(finiteNumber(data?.percent, 100)));
    const hardStop = data?.hard_stop === true || percent <= 0;
    const factorTemp = finiteNumber(data?.factor_temp, NaN);
    const factorRain = finiteNumber(data?.factor_rain, NaN);
    const factorHum = finiteNumber(data?.factor_humidity, NaN);
    const factorWind = finiteNumber(data?.factor_wind, NaN);
    const factorTotal = finiteNumber(data?.factor_total, NaN);
    const tempNow = finiteNumber(data?.temp_now, NaN);
    const rain24h = finiteNumber(data?.rain_24h, NaN);
    const humNow = finiteNumber(data?.humidity_now, NaN);
    const windNow = finiteNumber(data?.wind_now_kmh, NaN);
    const tempStop = finiteNumber(data?.threshold_temp_skip_c, NaN);
    const rainStop = finiteNumber(data?.threshold_rain_skip_mm, NaN);
    const humHigh = finiteNumber(data?.threshold_humidity_high_percent, NaN);
    const windStop = finiteNumber(data?.threshold_wind_skip_kmh, NaN);

    let statusClass = "normal";
    let statusText = tr("statusNormal");
    let dialColor = "#16a34a";
    if (hardStop) {
      statusClass = "stop";
      statusText = tr("statusStop");
      dialColor = "#c02626";
    } else if (percent < 100) {
      statusClass = "reduced";
      statusText = tr("statusReduced");
      dialColor = "#b45309";
    } else if (percent > 100) {
      statusClass = "extended";
      statusText = tr("statusExtended");
      dialColor = "#2563eb";
    }

    if (statusEl) {
      statusEl.className = `wms-weather-pct-status ${statusClass}`;
      statusEl.textContent = statusText;
    }
    if (valueEl) valueEl.textContent = `${percent}%`;
    if (dialEl) {
      dialEl.style.setProperty("--wms-dial-value", String(Math.max(0, Math.min(percent, 100))));
      dialEl.style.setProperty("--wms-dial-color", dialColor);
    }

    if (hardStop) {
      appendReason(reasonsEl, tr("hardStopTitle"), hardStopDescription(data), "0%", "stop");
      appendReason(reasonsEl, tr("skippedTitle"), tr("skippedDetail"), "-", "");
      if (footEl) {
        const explain = String(data?.explain || "").trim();
        footEl.textContent = explain ? `${tr("explainPrefix")}: ${explain}` : "";
      }
      return;
    }

    const tempTone = Number.isFinite(factorTemp) && factorTemp < 0.999
      ? "down"
      : (Number.isFinite(factorTemp) && factorTemp > 1.001 ? "up" : "");
    const rainTone = Number.isFinite(factorRain) && factorRain < 0.999
      ? "down"
      : (Number.isFinite(factorRain) && factorRain > 1.001 ? "up" : "");
    const humTone = Number.isFinite(factorHum) && factorHum < 0.999
      ? "down"
      : (Number.isFinite(factorHum) && factorHum > 1.001 ? "up" : "");
    const windTone = Number.isFinite(factorWind) && factorWind < 0.999
      ? "down"
      : (Number.isFinite(factorWind) && factorWind > 1.001 ? "up" : "");

    const tempDetail = tempTone === "down"
      ? tr("tempDetailReduce", { temp: fmt(tempNow, 1, "-"), threshold: fmt(tempStop, 1, "-") })
      : (tempTone === "up"
        ? tr("tempDetailBoost", { temp: fmt(tempNow, 1, "-") })
        : tr("tempDetailNeutral", { threshold: fmt(tempStop, 1, "-") }));
    const rainDetail = rainTone === "down"
      ? tr("rainDetailReduce", { rain: fmt(rain24h, 1, "-"), threshold: fmt(rainStop, 1, "-") })
      : tr("rainDetailNeutral", { threshold: fmt(rainStop, 1, "-") });
    const humDetail = humTone === "down"
      ? tr("humDetailReduce", { value: fmt(humNow, 0, "-"), threshold: fmt(humHigh, 0, "-") })
      : tr("humDetailNeutral", { threshold: fmt(humHigh, 0, "-") });
    const windDetail = windTone
      ? tr("windDetailAdjust", { threshold: fmt(windStop, 1, "-") })
      : tr("windDetailNeutral", { threshold: fmt(windStop, 1, "-") });

    appendReason(
      reasonsEl,
      tr("tempTitle", { value: fmt(tempNow, 1, "-") }),
      tempDetail,
      fmtFactor(factorTemp),
      tempTone
    );
    appendReason(
      reasonsEl,
      tr("rainTitle", { value: fmt(rain24h, 1, "-") }),
      rainDetail,
      fmtFactor(factorRain),
      rainTone
    );
    appendReason(
      reasonsEl,
      tr("humTitle", { value: fmt(humNow, 0, "-") }),
      humDetail,
      fmtFactor(factorHum),
      humTone
    );
    appendReason(
      reasonsEl,
      tr("windTitle", { value: fmt(windNow, 1, "-") }),
      windDetail,
      fmtFactor(factorWind),
      windTone
    );

    const rawPercent = Number.isFinite(factorTotal) ? Math.round(factorTotal * 100) : NaN;
    const minPct = Math.round(finiteNumber(data?.percent_min, 0));
    const maxPct = Math.round(finiteNumber(data?.percent_max, 300));
    if (Number.isFinite(rawPercent) && rawPercent !== percent) {
      appendReason(
        reasonsEl,
        tr("clampTitle"),
        tr("clampDetail", { raw: rawPercent, min: minPct, max: maxPct }),
        `${percent}%`,
        ""
      );
    }

    if (footEl) {
      const formula = tr("formula", {
        t: fmt(factorTemp, 2, "1.00"),
        r: fmt(factorRain, 2, "1.00"),
        h: fmt(factorHum, 2, "1.00"),
        w: fmt(factorWind, 2, "1.00"),
        p: percent,
      });
      const explain = String(data?.explain || "").trim();
      footEl.innerHTML = `<strong>${formula}</strong>${explain ? `<div class="wms-weather-pct-explain">${tr("explainPrefix")}: ${explain}</div>` : ""}`;
    }
  }

  async function fetchPercent(force) {
    const hasWeather = !!getWeatherContainer();
    if (!hasWeather) return;
    if (percentLoading) return;
    const now = Date.now();
    if (!force && now - lastFetchTs < 9000) return;
    percentLoading = true;
    lastFetchTs = now;
    scheduleRefresh();
    try {
      const resp = await fetch("/api/watering-percent", {
        credentials: "same-origin",
        cache: "no-store",
      });
      if (!resp.ok) {
        throw new Error(`HTTP ${resp.status}`);
      }
      const data = await resp.json().catch(() => null);
      if (!data || typeof data !== "object") {
        throw new Error("Invalid payload");
      }
      percentData = data;
      percentError = "";
    } catch (err) {
      percentError = String(err?.message || err || "");
    } finally {
      percentLoading = false;
      scheduleRefresh();
    }
  }

  async function fetchSettings(force) {
    if (settingsLoading) return;
    const now = Date.now();
    if (!force && now - lastSettingsFetchTs < 12000) return;
    settingsLoading = true;
    lastSettingsFetchTs = now;
    scheduleRefresh();
    try {
      const resp = await fetch("/api/settings", {
        credentials: "same-origin",
        cache: "no-store",
      });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json().catch(() => null);
      if (!data || typeof data !== "object") throw new Error("Invalid settings payload");
      settingsData = data;
    } catch {
      // Keep last known settings snapshot if request failed.
    } finally {
      settingsLoading = false;
      scheduleRefresh();
    }
  }

  function updateView() {
    if (domObserver && !observerPaused) {
      observerPaused = true;
      domObserver.disconnect();
    }
    try {
      ensureStyle();
      patchAutoModeDescription();
      patchSecretPlaceholders();
      const section = getWeatherContainer();
      const existingCard = document.getElementById(CARD_ID);
      if (!(section instanceof HTMLElement)) {
        if (existingCard) existingCard.remove();
        revealLegacyCards();
        return;
      }

      const legacyCard = findLegacyWeatherCard(section);
      if (legacyCard instanceof HTMLElement) {
        legacyCard.style.display = "none";
        legacyCard.classList.add("wms-weather-legacy-hidden");
        legacyCard.setAttribute(LEGACY_ATTR, "1");
      }

      const card = ensureCard(section, legacyCard);
      renderCard(card);
      wireRainStatsTrigger(section);
      const rainModal = document.getElementById(RAIN_STATS_MODAL_ID);
      if (rainModal instanceof HTMLElement && rainModal.classList.contains("show")) {
        ensureRainStatsModal();
      }
    } finally {
      if (domObserver) {
        observerPaused = false;
        domObserver.observe(document.getElementById("root") || document.body || document.documentElement, observerConfig);
      }
    }
  }

  function scheduleRefresh(forceFetch) {
    if (forceFetch) forceFetchQueued = true;
    if (refreshQueued) return;
    refreshQueued = true;
    requestAnimationFrame(() => {
      refreshQueued = false;
      updateView();
      if (forceFetchQueued) {
        forceFetchQueued = false;
        fetchPercent(true);
      }
    });
  }

  function start() {
    scheduleRefresh(true);
    fetchSettings(true);
    domObserver = new MutationObserver(() => {
      if (observerPaused) return;
      scheduleRefresh(false);
    });
    domObserver.observe(document.getElementById("root") || document.body || document.documentElement, observerConfig);
    setInterval(() => {
      fetchPercent(false);
      fetchSettings(false);
      fetchRainStats(false);
    }, 15000);
    window.addEventListener("focus", () => {
      fetchPercent(true);
      fetchSettings(true);
    }, { passive: true });
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden) {
        fetchPercent(true);
        fetchSettings(true);
      }
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start, { once: true });
  } else {
    start();
  }
})();
