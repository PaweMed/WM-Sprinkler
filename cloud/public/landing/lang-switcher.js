(function () {
  const langSwitcher = document.getElementById("lang-switcher");
  const langTrigger = document.getElementById("lang-trigger");
  const langMenu = document.getElementById("lang-menu");
  const langOptions = Array.from(document.querySelectorAll("#lang-menu .lang-option"));

  if (!langSwitcher || !langTrigger || !langMenu || !langOptions.length) return;

  const descriptionMeta = document.querySelector('meta[name="description"]');
  const storageKey = "wm-landing-language";
  const languageCookieKey = "wms_lang";
  const supportedLanguages = ["pl", "en", "de", "es"];
  const languageFlags = {
    pl: "🇵🇱",
    en: "🇬🇧",
    de: "🇩🇪",
    es: "🇪🇸"
  };
  const defaultLanguage = "pl";
  const fallbackLanguage = "en";

  function languageFromPath(pathname) {
    const segment = String(pathname || "")
      .replace(/^\/+|\/+$/g, "")
      .split("/")[0]
      .toLowerCase();
    return supportedLanguages.includes(segment) ? segment : "";
  }

  function languageUrl(langCode) {
    const search = window.location.search || "";
    const hash = window.location.hash || "";
    return `/${langCode}${search}${hash}`;
  }

  function syncLanguageUrl(langCode, replace) {
    const historyApi = window.history;
    if (!historyApi || (typeof historyApi.pushState !== "function" && typeof historyApi.replaceState !== "function")) return;
    const target = languageUrl(langCode);
    const current = `${window.location.pathname}${window.location.search}${window.location.hash}`;
    if (current === target) return;
    if (replace && typeof historyApi.replaceState === "function") historyApi.replaceState(null, "", target);
    else historyApi.pushState(null, "", target);
  }

  function upsertHeadLink(id, attrs) {
    let link = document.getElementById(id);
    if (!link) {
      link = document.createElement("link");
      link.id = id;
      document.head.appendChild(link);
    }
    Object.entries(attrs).forEach(([key, value]) => link.setAttribute(key, value));
  }

  function absoluteLanguageUrl(langCode) {
    try {
      return new URL(`/${langCode}`, window.location.origin).toString();
    } catch (_) {
      return `/${langCode}`;
    }
  }

  function updateSeoLinks(langCode) {
    upsertHeadLink("wm-canonical", {
      rel: "canonical",
      href: absoluteLanguageUrl(langCode)
    });
    supportedLanguages.forEach((code) => {
      upsertHeadLink(`wm-hreflang-${code}`, {
        rel: "alternate",
        hreflang: code,
        href: absoluteLanguageUrl(code)
      });
    });
    upsertHeadLink("wm-hreflang-x-default", {
      rel: "alternate",
      hreflang: "x-default",
      href: absoluteLanguageUrl(fallbackLanguage)
    });
  }

  function getCookieValue(name) {
    const encodedName = `${encodeURIComponent(name)}=`;
    const raw = String(document.cookie || "");
    const cookie = raw.split(";").map((x) => x.trim()).find((entry) => entry.startsWith(encodedName));
    if (!cookie) return "";
    try {
      return decodeURIComponent(cookie.slice(encodedName.length));
    } catch (_) {
      return "";
    }
  }

  function storeLanguageCookie(langCode) {
    const maxAgeSeconds = 60 * 60 * 24 * 365 * 2;
    const securePart = window.location.protocol === "https:" ? "; Secure" : "";
    document.cookie = `${encodeURIComponent(languageCookieKey)}=${encodeURIComponent(langCode)}; Path=/; Max-Age=${maxAgeSeconds}; SameSite=Lax${securePart}`;
  }

  const nodes = {
    brandSub: document.querySelector(".brand-sub"),
    loginBtn: document.querySelector(".login-btn"),
    menuLabel: document.querySelector(".menu-label"),
    menuLinks: Array.from(document.querySelectorAll(".menu a")),
    heroSection: document.querySelector(".hero"),
    heroTitle: document.querySelector(".hero h1"),
    heroBody: document.querySelector(".hero p"),
    heroBadges: Array.from(document.querySelectorAll(".hero-badges span")),
    projectTitle: document.querySelector("#projekt h2"),
    projectBody: document.querySelector("#projekt p"),
    integrationsTitle: document.querySelector("#integracje h2"),
    integrationsBody: document.querySelector("#integracje p"),
    assumptionsTitle: document.querySelector("#zalozenia h2"),
    assumptionsLead: document.querySelector("#zalozenia .cap-lead"),
    assumptionsHeroTitle: document.querySelector("#zalozenia .cap-hero-copy h3"),
    assumptionsHeroBody: document.querySelector("#zalozenia .cap-hero-copy p"),
    assumptionsHeroChip: document.querySelector("#zalozenia .cap-hero-visual .visual-chip"),
    assumptionsHeroTagline: document.querySelector("#zalozenia .cap-hero-visual span"),
    assumptionsCards: Array.from(document.querySelectorAll("#zalozenia .cap-grid .cap-card")),
    capabilitiesTitle: document.querySelector("#mozliwosci h2"),
    capabilitiesLead: document.querySelector("#mozliwosci .cap-lead"),
    capabilitiesHeroTitle: document.querySelector("#mozliwosci .cap-hero-copy h3"),
    capabilitiesHeroBody: document.querySelector("#mozliwosci .cap-hero-copy p"),
    capabilitiesHeroChip: document.querySelector("#mozliwosci .cap-hero-visual .visual-chip"),
    capabilitiesHeroTagline: document.querySelector("#mozliwosci .cap-hero-visual span"),
    capabilitiesCards: Array.from(document.querySelectorAll("#mozliwosci .cap-grid .cap-card")),
    developmentTitle: document.querySelector("#rozwoj h2"),
    developmentLead: document.querySelector("#rozwoj .dev-lead"),
    developmentHeroTitle: document.querySelector("#rozwoj .dev-hero-copy h3"),
    developmentHeroBody: document.querySelector("#rozwoj .dev-hero-copy p"),
    developmentHeroChip: document.querySelector("#rozwoj .dev-hero-visual .visual-chip"),
    developmentHeroTagline: document.querySelector("#rozwoj .dev-hero-visual span"),
    developmentCards: Array.from(document.querySelectorAll("#rozwoj .dev-grid .dev-card")),
    roadmapSection: document.querySelector("#rozwoj .dev-roadmap"),
    roadmapTitle: document.querySelector("#rozwoj .dev-roadmap h3"),
    roadmapLead: document.querySelector("#rozwoj .dev-roadmap > p"),
    roadmapItems: Array.from(document.querySelectorAll("#rozwoj .roadmap-item")),
    aboutTitle: document.querySelector("#o-nas h2"),
    aboutBody: document.querySelector("#o-nas p"),
    footerNote: document.querySelector(".footer-note")
  };

  function textOf(el) {
    return el ? el.textContent.trim() : "";
  }

  function htmlOf(el) {
    return el ? el.innerHTML.trim() : "";
  }

  function cloneHeadingText(headingEl) {
    if (!headingEl) return "";
    const clone = headingEl.cloneNode(true);
    clone.querySelectorAll(".icon").forEach((icon) => icon.remove());
    return clone.textContent.trim();
  }

  function listText(elements) {
    return elements.map((el) => textOf(el));
  }

  function captureCards(cards) {
    return cards.map((card) => {
      const heading = card.querySelector("h3");
      const body = card.querySelector("p:not(.dev-emphasis)");
      const emphasis = card.querySelector(".dev-emphasis");
      return {
        title: cloneHeadingText(heading),
        body: textOf(body),
        list: listText(Array.from(card.querySelectorAll("ul li"))),
        emphasis: emphasis ? textOf(emphasis) : ""
      };
    });
  }

  function captureRoadmap(items) {
    return items.map((item) => ({
      title: textOf(item.querySelector(".roadmap-copy h4")),
      list: listText(Array.from(item.querySelectorAll(".roadmap-copy ul li")))
    }));
  }

  const polishContent = {
    meta: {
      title: document.title,
      description: descriptionMeta ? descriptionMeta.getAttribute("content") || "" : ""
    },
    ui: {
      brandSub: textOf(nodes.brandSub),
      loginBtn: textOf(nodes.loginBtn),
      menuLabel: textOf(nodes.menuLabel),
      menuLinks: listText(nodes.menuLinks),
      heroAria: nodes.heroSection ? nodes.heroSection.getAttribute("aria-label") || "" : "",
      langTriggerLabel: "Wybierz język",
      langMenuLabel: "Wybór języka"
    },
    hero: {
      title: textOf(nodes.heroTitle),
      body: textOf(nodes.heroBody),
      badges: listText(nodes.heroBadges)
    },
    project: {
      title: textOf(nodes.projectTitle),
      body: textOf(nodes.projectBody)
    },
    integrations: {
      title: textOf(nodes.integrationsTitle),
      body: textOf(nodes.integrationsBody)
    },
    assumptions: {
      title: textOf(nodes.assumptionsTitle),
      lead: textOf(nodes.assumptionsLead),
      heroTitle: textOf(nodes.assumptionsHeroTitle),
      heroBody: textOf(nodes.assumptionsHeroBody),
      heroChip: textOf(nodes.assumptionsHeroChip),
      heroTagline: textOf(nodes.assumptionsHeroTagline),
      cards: captureCards(nodes.assumptionsCards)
    },
    capabilities: {
      title: textOf(nodes.capabilitiesTitle),
      lead: textOf(nodes.capabilitiesLead),
      heroTitle: textOf(nodes.capabilitiesHeroTitle),
      heroBody: textOf(nodes.capabilitiesHeroBody),
      heroChip: textOf(nodes.capabilitiesHeroChip),
      heroTagline: textOf(nodes.capabilitiesHeroTagline),
      cards: captureCards(nodes.capabilitiesCards)
    },
    development: {
      title: textOf(nodes.developmentTitle),
      lead: textOf(nodes.developmentLead),
      heroTitle: textOf(nodes.developmentHeroTitle),
      heroBody: textOf(nodes.developmentHeroBody),
      heroChip: textOf(nodes.developmentHeroChip),
      heroTagline: textOf(nodes.developmentHeroTagline),
      cards: captureCards(nodes.developmentCards),
      roadmapAria: nodes.roadmapSection ? nodes.roadmapSection.getAttribute("aria-label") || "" : "",
      roadmapTitle: textOf(nodes.roadmapTitle),
      roadmapLead: textOf(nodes.roadmapLead),
      roadmap: captureRoadmap(nodes.roadmapItems)
    },
    about: {
      title: textOf(nodes.aboutTitle),
      bodyHtml: htmlOf(nodes.aboutBody)
    },
    footer: {
      note: textOf(nodes.footerNote)
    }
  };

  const translations = {
    en: {
      meta: {
        title: "WM Sprinkler | Smart Irrigation",
        description: "WM Sprinkler - professional smart irrigation system developed by PaweMed."
      },
      ui: {
        brandSub: "Professional smart irrigation system",
        loginBtn: "Sign in",
        menuLabel: "Menu",
        menuLinks: ["Project overview", "Integrations", "Foundations", "Capabilities", "Development", "About us"],
        heroAria: "Introduction",
        langTriggerLabel: "Choose language",
        langMenuLabel: "Language selection"
      },
      hero: {
        title: "Modern irrigation management for homes, gardens, and professional installations",
        body: "WM Sprinkler combines a reliable device controller, a cloud panel, and Home Assistant integration. We continuously develop the project with a strong focus on quality, security, and daily usability.",
        badges: ["ESP32 + Cloud", "MQTT + Home Assistant", "OTA and online monitoring"]
      },
      project: {
        title: "Project overview",
        body: "The system consists of device firmware, a user panel, an admin panel, and a cloud layer. This gives users full control over zones, schedules, and operating parameters while also providing secure remote access and automated updates."
      },
      integrations: {
        title: "Integrations",
        body: "The key integration is MQTT with Home Assistant, where the device appears automatically via discovery. Users can read states and control zones without manually configuring many entities. The architecture also allows easy expansion to additional platforms."
      },
      assumptions: {
        title: "WM Sprinkler project foundations",
        lead: "WM Sprinkler was created from one core need: to build an irrigation system that is intelligent, reliable, and uncompromising. From day one, we have developed it not as another controller, but as a modern technology platform combining automation, data analysis, and convenient remote management.",
        heroTitle: "Project foundation",
        heroBody: "The assumption is simple: the garden should get exactly as much water as it needs, and the user should have full control without constantly supervising the system. Technology should simplify everyday life, not complicate it.",
        heroChip: "Data-driven irrigation",
        heroTagline: "Intelligent + reliable + open",
        cards: [
          {
            title: "Intelligence instead of rigid schedules",
            body: "WM Sprinkler reacts to real environmental conditions, not just to a saved timer.",
            list: ["weather analysis", "rainfall awareness", "temperature response", "water dose adjustment"],
            emphasis: "A garden does not follow a clock. It follows nature."
          },
          {
            title: "Maximum water savings",
            body: "Efficient water usage is one of the key pillars of the entire system.",
            list: ["limit watering after rain", "reduce doses at high humidity", "optimize zone runtime", "eliminate unnecessary cycles"]
          },
          {
            title: "Automation that simplifies daily life",
            body: "The system is designed to work in the background and reduce day-to-day manual effort.",
            list: ["automatic watering adjustments", "intelligent schedules", "self-learning algorithms", "water demand prediction"]
          },
          {
            title: "Availability without limits",
            body: "A modern system should be available anytime and anywhere.",
            list: ["local operation (offline)", "remote access (online)", "secure communication", "fast, simple control"]
          },
          {
            title: "Openness and scalability",
            body: "The modular architecture allows the system to grow with the user's needs.",
            list: ["expand with additional zones", "sensor integration", "OTA updates", "smart-home integrations and custom modifications"],
            emphasis: "This is a development platform, not a closed product."
          },
          {
            title: "Technological independence",
            body: "WM Sprinkler requires no paid subscriptions and gives full control over data.",
            list: ["fully local operation", "works without internet", "no fees for core features", "full ownership and control of user data"]
          },
          {
            title: "Long-term vision",
            body: "The goal is to create one of the most advanced and accessible smart-class systems.",
            list: ["environment learning", "resource consumption optimization", "future-home integration", "multi-year platform growth"],
            emphasis: "WM Sprinkler is the foundation for smart water management: today in the garden, tomorrow in larger installations."
          }
        ]
      },
      capabilities: {
        title: "WM Sprinkler capabilities",
        lead: "WM Sprinkler is an advanced irrigation control system that combines automation precision with the convenience of a modern web app. It was designed to provide full control over watering both locally and remotely.",
        heroTitle: "Full control over your garden",
        heroBody: "Zone control, schedules, weather automation, monitoring, and remote access work together in one consistent system. This makes every watering cycle precise and predictable.",
        heroChip: "Multi-zone control",
        heroTagline: "Automation + cloud + real-time",
        cards: [
          {
            title: "Multi-zone control",
            body: "Each zone can work independently and have its own configuration.",
            list: ["dedicated schedule", "individual watering time", "manual and automatic mode", "dynamic watering percentage"],
            emphasis: "You can tune lawns, flower beds, greenhouses, and drip lines separately."
          },
          {
            title: "Advanced scheduling",
            body: "The system offers flexible planning and control of zone work sequences.",
            list: ["daily and weekly schedules", "multiple programs in parallel", "defined start times", "active days by calendar"]
          },
          {
            title: "Automatic weather adjustment",
            body: "WM Sprinkler analyzes conditions and dynamically adjusts irrigation intensity.",
            list: ["limit watering after rainfall", "increase dose in high temperatures", "pause watering during rain", "adaptive irrigation percentage"]
          },
          {
            title: "Real-time monitoring",
            body: "In the app, you have live insight into system operation and history.",
            list: ["active zone and remaining time", "run history", "automatic correction percentage", "device connection status"]
          },
          {
            title: "Local and remote access",
            body: "The system works locally, but also provides secure internet access.",
            list: ["remote control from anywhere", "status overview away from home", "event notifications", "full control even on vacation"]
          },
          {
            title: "Notifications and integrations",
            body: "The platform reports key events and connects easily with home automation.",
            list: ["watering start and finish", "system errors and status changes", "weather events", "MQTT, Home Assistant, and smart-home"]
          },
          {
            title: "OTA updates",
            body: "You can update firmware over the network without dismantling the controller.",
            list: ["new features without visiting the device", "security and stability fixes", "continuous growth without extra hardware"]
          },
          {
            title: "Modular architecture",
            body: "A modern ESP32 base provides flexibility and long-term durability.",
            list: ["expand with sensors", "integrate with smart-home systems", "scale number of zones", "personalize user settings"]
          }
        ]
      },
      development: {
        title: "WM Sprinkler development",
        lead: "WM Sprinkler is more than a watering controller. It is a modern intelligent irrigation platform designed for maximum convenience, water savings, and full automation of garden operation. From the first line of code, we have built this system as a premium-class solution that reacts to environmental conditions in real time.",
        heroTitle: "Development direction",
        heroBody: "We combine irrigation automation, weather data, analytics, and secure cloud access. The goal is a system that becomes more precise each season and truly helps save water.",
        heroChip: "AI Water Logic",
        heroTagline: "Smart irrigation platform",
        cards: [
          {
            title: "Intelligent water savings",
            body: "The system analyzes weather data and decides how much water is really needed.",
            list: ["rainfall", "temperature", "air humidity", "weather forecasts"],
            emphasis: "Result: lower water bills and healthier plants, without manual intervention."
          },
          {
            title: "Automation that learns your garden",
            body: "We are developing AI Water Logic, an algorithm that analyzes watering history and plant needs.",
            list: ["water demand prediction", "zone duration optimization", "seasonality awareness", "automatic schedule suggestions"]
          },
          {
            title: "Control from anywhere",
            body: "Cloud architecture provides secure access to the system whenever you need it.",
            list: ["zone status overview", "manual watering start", "schedule changes", "event notifications"]
          },
          {
            title: "Data that matters",
            body: "We are building WM Sprinkler as a center for garden analytics and real savings.",
            list: ["water usage overview", "watering statistics", "monthly and seasonal comparisons", "savings estimation"]
          },
          {
            title: "Modern interface",
            body: "The interface is developed in a premium smart-home app style.",
            list: ["responsive web app (PWA)", "phone, tablet, and desktop support", "dedicated wall panels", "quick system status preview"]
          },
          {
            title: "Future-ready technology",
            body: "The ESP32 platform delivers stability, performance, and strong expansion potential.",
            list: ["high computing power", "modular sensor expansion", "OTA updates", "long-term project durability"]
          }
        ],
        roadmapAria: "Roadmap",
        roadmapTitle: "Development roadmap",
        roadmapLead: "WM Sprinkler development plan from system foundations to a full smart-home ecosystem.",
        roadmap: [
          {
            title: "System foundation",
            list: ["controller architecture", "ESP32 zone control", "watering schedules", "local web panel and manual/automatic modes"]
          },
          {
            title: "Intelligent automation",
            list: ["weather data integration", "dynamic watering percentage", "rain history (rain buffer)", "Push/MQTT notifications and Home Assistant"]
          },
          {
            title: "Cloud and remote access",
            list: ["user accounts", "online device registration", "cloud panel and settings sync", "remote firmware OTA"]
          },
          {
            title: "AI and analytics",
            list: ["AI Water Logic", "self-learning schedules", "water demand prediction", "advanced statistics and reports"]
          },
          {
            title: "WM Sprinkler ecosystem",
            list: ["soil moisture sensors", "water flow sensors", "zone extension modules", "PRO version and Matter/Thread integrations"]
          }
        ]
      },
      about: {
        title: "About us",
        bodyHtml: "WM Sprinkler is a team that designs and develops its own automatic irrigation system. We focus on practical solutions: stable operation, simple deployment, and convenient management both locally and online.<br><br>Contact: PaweMed, email: <a href=\"mailto:witkowski.med@gmail.com\">witkowski.med@gmail.com</a>"
      },
      footer: {
        note: "PaweMed 2026"
      }
    },
    de: {
      meta: {
        title: "WM Sprinkler | Intelligente Bewässerung",
        description: "WM Sprinkler - professionelles intelligentes Bewässerungssystem, entwickelt von PaweMed."
      },
      ui: {
        brandSub: "Professionelles intelligentes Bewässerungssystem",
        loginBtn: "Anmelden",
        menuLabel: "Menü",
        menuLinks: ["Projektübersicht", "Integrationen", "Grundlagen", "Funktionen", "Entwicklung", "Über uns"],
        heroAria: "Einführung",
        langTriggerLabel: "Sprache wählen",
        langMenuLabel: "Sprachauswahl"
      },
      hero: {
        title: "Moderne Bewässerungssteuerung für Haus, Garten und professionelle Anlagen",
        body: "WM Sprinkler verbindet einen zuverlässigen Gerätesteuerer, ein Cloud-Panel und die Integration mit Home Assistant. Das Projekt wird kontinuierlich weiterentwickelt, mit Fokus auf Qualität, Sicherheit und komfortable tägliche Nutzung.",
        badges: ["ESP32 + Cloud", "MQTT + Home Assistant", "OTA und Online-Monitoring"]
      },
      project: {
        title: "Projektübersicht",
        body: "Das System besteht aus Geräte-Firmware, Benutzerpanel, Administrationspanel und einer Cloud-Schicht. Dadurch erhält der Nutzer volle Kontrolle über Zonen, Zeitpläne und Betriebsparameter sowie sicheren Fernzugriff und automatisierte Updates."
      },
      integrations: {
        title: "Integrationen",
        body: "Die wichtigste Integration ist MQTT mit Home Assistant, wo das Gerät automatisch per Discovery erscheint. Der Nutzer kann Zustände auslesen und Zonen steuern, ohne viele Entitäten manuell zu konfigurieren. Die Architektur ermöglicht außerdem die Erweiterung um weitere Plattformen."
      },
      assumptions: {
        title: "Grundlagen des WM Sprinkler Projekts",
        lead: "WM Sprinkler entstand aus einem zentralen Bedarf: ein Bewässerungssystem zu schaffen, das intelligent, zuverlässig und kompromisslos arbeitet. Von Anfang an entwickeln wir es nicht als weiteren Controller, sondern als moderne Technologieplattform, die Automatisierung, Datenanalyse und komfortable Fernverwaltung verbindet.",
        heroTitle: "Projektfundament",
        heroBody: "Die Annahme ist einfach: Der Garten soll genau so viel Wasser bekommen, wie er benötigt, und der Nutzer soll volle Kontrolle haben, ohne das System ständig überwachen zu müssen. Technologie soll den Alltag vereinfachen, nicht verkomplizieren.",
        heroChip: "Data-driven irrigation",
        heroTagline: "Intelligent + reliable + open",
        cards: [
          {
            title: "Intelligenz statt starrer Schemata",
            body: "WM Sprinkler reagiert auf reale Umweltbedingungen und nicht nur auf einen gespeicherten Zeitplan.",
            list: ["Wetteranalyse", "Berücksichtigung von Niederschlägen", "Reaktion auf Temperatur", "Anpassung der Wassermenge"],
            emphasis: "Ein Garten arbeitet nicht nach der Uhr. Er folgt der Natur."
          },
          {
            title: "Maximale Wassereinsparung",
            body: "Ein effizienter Umgang mit Wasser ist eine der zentralen Säulen des gesamten Systems.",
            list: ["Bewässerung nach Regen begrenzen", "Mengen bei hoher Luftfeuchtigkeit reduzieren", "Sektorlaufzeiten optimieren", "unnötige Zyklen eliminieren"]
          },
          {
            title: "Automatisierung, die das Leben erleichtert",
            body: "Das System soll im Hintergrund arbeiten und den Nutzer im Alltag entlasten.",
            list: ["automatische Bewässerungskorrekturen", "intelligente Zeitpläne", "selbstlernende Algorithmen", "Prognose des Wasserbedarfs"]
          },
          {
            title: "Verfügbarkeit ohne Grenzen",
            body: "Ein modernes System sollte immer und überall verfügbar sein.",
            list: ["lokaler Betrieb (offline)", "Fernzugriff (online)", "sichere Kommunikation", "schnelle, einfache Bedienung"]
          },
          {
            title: "Offenheit und Skalierbarkeit",
            body: "Die modulare Architektur ermöglicht die Weiterentwicklung entsprechend den Nutzeranforderungen.",
            list: ["Erweiterung um weitere Zonen", "Sensorintegration", "OTA-Updates", "Smart-Home-Integrationen und eigene Anpassungen"],
            emphasis: "Das ist eine Entwicklungsplattform, kein geschlossenes Produkt."
          },
          {
            title: "Technologische Unabhängigkeit",
            body: "WM Sprinkler benötigt keine kostenpflichtigen Abonnements und bietet volle Datenkontrolle.",
            list: ["vollständig lokaler Betrieb", "funktioniert ohne Internet", "keine Gebühren für Kernfunktionen", "volle Eigentümerschaft und Kontrolle über Nutzerdaten"]
          },
          {
            title: "Langfristige Vision",
            body: "Ziel ist es, eines der fortschrittlichsten und zugänglichsten Smart-Systeme zu schaffen.",
            list: ["Lernen der Umgebung", "Optimierung des Ressourcenverbrauchs", "Integration in das Zuhause der Zukunft", "mehrjährige Plattformentwicklung"],
            emphasis: "WM Sprinkler ist das Fundament für intelligentes Wassermanagement: heute im Garten, morgen in größeren Anlagen."
          }
        ]
      },
      capabilities: {
        title: "WM Sprinkler Funktionen",
        lead: "WM Sprinkler ist ein fortschrittliches Bewässerungssteuerungssystem, das die Präzision der Automatisierung mit dem Komfort einer modernen Web-App verbindet. Es wurde entwickelt, um vollständige Kontrolle über die Bewässerung lokal und aus der Ferne zu bieten.",
        heroTitle: "Volle Kontrolle über den Garten",
        heroBody: "Zonensteuerung, Zeitpläne, Wetterautomatisierung, Monitoring und Fernzugriff arbeiten in einem konsistenten System zusammen. So ist jede Bewässerung präzise und vorhersehbar.",
        heroChip: "Multi-zone control",
        heroTagline: "Automation + cloud + real-time",
        cards: [
          {
            title: "Steuerung mehrerer Zonen",
            body: "Jede Zone kann unabhängig arbeiten und eine eigene Konfiguration haben.",
            list: ["eigener Zeitplan", "individuelle Bewässerungsdauer", "manueller und automatischer Modus", "dynamischer Bewässerungsprozentsatz"],
            emphasis: "Rasen, Beete, Gewächshaus und Tropfleitungen können separat angepasst werden."
          },
          {
            title: "Erweiterte Zeitpläne",
            body: "Das System bietet flexible Planung und Kontrolle der Zonensequenzen.",
            list: ["tägliche und wöchentliche Zeitpläne", "mehrere Programme parallel", "definierte Startzeiten", "aktive Tage nach Kalender"]
          },
          {
            title: "Automatische Wetterkorrektur",
            body: "WM Sprinkler analysiert Bedingungen und passt die Bewässerungsintensität dynamisch an.",
            list: ["Bewässerung nach Niederschlag begrenzen", "Menge bei hohen Temperaturen erhöhen", "Bewässerung bei Regen pausieren", "adaptiver Bewässerungsprozentsatz"]
          },
          {
            title: "Echtzeit-Monitoring",
            body: "In der App erhalten Sie laufend Einblick in Betrieb und Verlauf des Systems.",
            list: ["aktive Zone und Restzeit", "Startverlauf", "automatischer Korrekturprozentsatz", "Geräteverbindungsstatus"]
          },
          {
            title: "Lokaler und Fernzugriff",
            body: "Das System arbeitet lokal, bietet aber auch sicheren Internetzugriff.",
            list: ["Fernsteuerung von überall", "Statusübersicht außerhalb des Hauses", "Ereignisbenachrichtigungen", "volle Kontrolle auch im Urlaub"]
          },
          {
            title: "Benachrichtigungen und Integrationen",
            body: "Die Plattform meldet wichtige Ereignisse und lässt sich leicht mit Hausautomatisierung verbinden.",
            list: ["Start und Ende der Bewässerung", "Systemfehler und Statusänderungen", "Wetterereignisse", "MQTT, Home Assistant und Smart-Home"]
          },
          {
            title: "OTA-Updates",
            body: "Die Firmware wird über das Netzwerk aktualisiert, ohne den Controller zu demontieren.",
            list: ["neue Funktionen ohne Vor-Ort-Zugriff", "Sicherheits- und Stabilitätskorrekturen", "kontinuierliche Entwicklung ohne Zusatzhardware"]
          },
          {
            title: "Modulare Architektur",
            body: "Eine moderne ESP32-Basis bietet Flexibilität und langfristige Beständigkeit.",
            list: ["Erweiterung um Sensoren", "Integration mit Smart-Home-Systemen", "Skalierung der Zonenzahl", "Personalisierung von Benutzereinstellungen"]
          }
        ]
      },
      development: {
        title: "WM Sprinkler Entwicklung",
        lead: "WM Sprinkler ist mehr als ein Bewässerungscontroller. Es ist eine moderne Plattform für intelligentes Bewässerungsmanagement, entwickelt für maximalen Komfort, Wassereinsparung und vollständige Automatisierung des Gartenbetriebs. Seit der ersten Codezeile bauen wir dieses System als Premium-Lösung, die in Echtzeit auf Umweltbedingungen reagiert.",
        heroTitle: "Entwicklungsrichtung",
        heroBody: "Wir verbinden Bewässerungsautomatisierung, Wetterdaten, Analytik und sicheren Cloud-Zugriff. Ziel ist ein System, das mit jeder Saison präziser arbeitet und tatsächlich Wasser spart.",
        heroChip: "AI Water Logic",
        heroTagline: "Smart irrigation platform",
        cards: [
          {
            title: "Intelligente Wassereinsparung",
            body: "Das System analysiert Wetterdaten und entscheidet selbst, wie viel Wasser wirklich benötigt wird.",
            list: ["Niederschlag", "Temperatur", "Luftfeuchtigkeit", "Wetterprognosen"],
            emphasis: "Ergebnis: geringere Wasserkosten und gesündere Pflanzen ohne manuelles Eingreifen."
          },
          {
            title: "Automatisierung, die den Garten lernt",
            body: "Wir entwickeln AI Water Logic - einen Algorithmus, der Bewässerungshistorie und Pflanzenbedarf analysiert.",
            list: ["Prognose des Wasserbedarfs", "Optimierung der Zonendauer", "Berücksichtigung von Saisonalität", "automatische Zeitplanvorschläge"]
          },
          {
            title: "Steuerung von überall",
            body: "Die Cloud-Architektur bietet sicheren Zugriff auf das System, wann immer Sie ihn brauchen.",
            list: ["Statusübersicht der Zonen", "manuelles Starten der Bewässerung", "Änderung von Zeitplänen", "Ereignisbenachrichtigungen"]
          },
          {
            title: "Daten mit Bedeutung",
            body: "WM Sprinkler wird als Zentrum für Gartenanalytik und echte Einsparungen entwickelt.",
            list: ["Überblick über Wasserverbrauch", "Bewässerungsstatistiken", "monatliche und saisonale Vergleiche", "Schätzung der Einsparungen"]
          },
          {
            title: "Moderne Oberfläche",
            body: "Die Oberfläche wird im Stil einer Premium-Smart-Home-App entwickelt.",
            list: ["responsive Web-App (PWA)", "Unterstützung für Telefon, Tablet und Desktop", "dedizierte Wandpanels", "schnelle Systemstatus-Übersicht"]
          },
          {
            title: "Zukunftssichere Technologie",
            body: "Die ESP32-Plattform bietet Stabilität, Leistung und großes Erweiterungspotenzial.",
            list: ["hohe Rechenleistung", "modulare Sensorerweiterung", "OTA-Updates", "langfristige Projektbeständigkeit"]
          }
        ],
        roadmapAria: "Roadmap",
        roadmapTitle: "Entwicklungs-Roadmap",
        roadmapLead: "WM Sprinkler Entwicklungsplan von den Systemgrundlagen bis zum vollständigen Smart-Home-Ökosystem.",
        roadmap: [
          {
            title: "Systemfundament",
            list: ["Controller-Architektur", "Zonensteuerung auf ESP32", "Bewässerungszeitpläne", "lokales Web-Panel sowie manuelle/automatische Modi"]
          },
          {
            title: "Intelligente Automatisierung",
            list: ["Integration von Wetterdaten", "dynamischer Bewässerungsprozentsatz", "Niederschlagshistorie (Rain Buffer)", "Push/MQTT-Benachrichtigungen und Home Assistant"]
          },
          {
            title: "Cloud und Fernzugriff",
            list: ["Benutzerkonten", "Online-Geräteregistrierung", "Cloud-Panel und Synchronisierung von Einstellungen", "remote Firmware-OTA"]
          },
          {
            title: "KI und Analytik",
            list: ["AI Water Logic", "selbstlernende Zeitpläne", "Prognose des Wasserbedarfs", "erweiterte Statistiken und Berichte"]
          },
          {
            title: "WM Sprinkler Ökosystem",
            list: ["Bodenfeuchtesensoren", "Wasserdurchflusssensoren", "Zonenerweiterungsmodule", "PRO-Version und Matter/Thread-Integrationen"]
          }
        ]
      },
      about: {
        title: "Über uns",
        bodyHtml: "WM Sprinkler ist ein Team, das ein eigenes automatisches Bewässerungssystem entwickelt. Wir konzentrieren uns auf praktische Lösungen: stabilen Betrieb, einfache Implementierung und komfortable Verwaltung lokal wie auch online.<br><br>Kontakt: PaweMed, E-Mail: <a href=\"mailto:witkowski.med@gmail.com\">witkowski.med@gmail.com</a>"
      },
      footer: {
        note: "PaweMed 2026"
      }
    },
    es: {
      meta: {
        title: "WM Sprinkler | Riego Inteligente",
        description: "WM Sprinkler - sistema profesional de riego inteligente desarrollado por PaweMed."
      },
      ui: {
        brandSub: "Sistema profesional de riego inteligente",
        loginBtn: "Iniciar sesión",
        menuLabel: "Menú",
        menuLinks: ["Descripción del proyecto", "Integraciones", "Fundamentos", "Capacidades", "Desarrollo", "Sobre nosotros"],
        heroAria: "Introducción",
        langTriggerLabel: "Elegir idioma",
        langMenuLabel: "Selección de idioma"
      },
      hero: {
        title: "Gestión moderna del riego para hogares, jardines e instalaciones profesionales",
        body: "WM Sprinkler combina un controlador de dispositivo fiable, un panel en la nube y la integración con Home Assistant. Desarrollamos el proyecto de forma continua, cuidando la calidad, la seguridad y la comodidad del uso diario.",
        badges: ["ESP32 + Cloud", "MQTT + Home Assistant", "OTA y monitorización online"]
      },
      project: {
        title: "Descripción del proyecto",
        body: "El sistema está compuesto por firmware del dispositivo, panel de usuario, panel administrativo y capa cloud. Esto permite al usuario tener control total sobre zonas, horarios y parámetros de funcionamiento, además de acceso remoto seguro y actualizaciones automatizadas."
      },
      integrations: {
        title: "Integraciones",
        body: "La integración principal es MQTT con Home Assistant, donde el dispositivo aparece automáticamente mediante discovery. El usuario puede consultar estados y controlar zonas sin configurar manualmente múltiples entidades. La arquitectura también permite ampliar integraciones a nuevas plataformas."
      },
      assumptions: {
        title: "Fundamentos del proyecto WM Sprinkler",
        lead: "WM Sprinkler nació de una necesidad principal: crear un sistema de riego que funcione de forma inteligente, fiable y sin compromisos. Desde el principio lo desarrollamos no como otro controlador, sino como una plataforma tecnológica moderna que une automatización, análisis de datos y gestión remota cómoda.",
        heroTitle: "Base del proyecto",
        heroBody: "La idea es simple: el jardín debe recibir exactamente la cantidad de agua que necesita, y el usuario debe tener control total sin vigilar constantemente el sistema. La tecnología debe simplificar el día a día, no complicarlo.",
        heroChip: "Data-driven irrigation",
        heroTagline: "Intelligent + reliable + open",
        cards: [
          {
            title: "Inteligencia en lugar de esquemas rígidos",
            body: "WM Sprinkler responde a condiciones ambientales reales, no solo a un reloj programado.",
            list: ["análisis del clima", "consideración de lluvias", "respuesta a la temperatura", "ajuste de dosis de agua"],
            emphasis: "Un jardín no funciona por reloj. Funciona según la naturaleza."
          },
          {
            title: "Máximo ahorro de agua",
            body: "El uso racional del agua es uno de los pilares clave de todo el sistema.",
            list: ["limitar riego después de la lluvia", "reducir dosis con alta humedad", "optimizar tiempo por zona", "eliminar ciclos innecesarios"]
          },
          {
            title: "Automatización que simplifica la vida",
            body: "El sistema está diseñado para funcionar en segundo plano y descargar al usuario en la operación diaria.",
            list: ["ajustes automáticos de riego", "horarios inteligentes", "algoritmos de autoaprendizaje", "predicción de demanda de agua"]
          },
          {
            title: "Disponibilidad sin límites",
            body: "Un sistema moderno debe estar disponible siempre y en cualquier lugar.",
            list: ["funcionamiento local (offline)", "acceso remoto (online)", "comunicación segura", "manejo rápido y sencillo"]
          },
          {
            title: "Apertura y escalabilidad",
            body: "La arquitectura modular permite que el sistema crezca junto con las necesidades del usuario.",
            list: ["ampliación con más zonas", "integración con sensores", "actualizaciones OTA", "integraciones smart-home y personalizaciones propias"],
            emphasis: "Es una plataforma de desarrollo, no un producto cerrado."
          },
          {
            title: "Independencia tecnológica",
            body: "WM Sprinkler no requiere suscripciones de pago y ofrece control total sobre los datos.",
            list: ["operación completamente local", "funciona sin internet", "sin cuotas para funciones básicas", "propiedad y control total de los datos del usuario"]
          },
          {
            title: "Visión a largo plazo",
            body: "El objetivo es crear uno de los sistemas smart más avanzados y accesibles.",
            list: ["aprendizaje del entorno", "optimización del consumo de recursos", "integración con el hogar del futuro", "desarrollo de plataforma a largo plazo"],
            emphasis: "WM Sprinkler es la base para la gestión inteligente del agua: hoy en el jardín, mañana en instalaciones mayores."
          }
        ]
      },
      capabilities: {
        title: "Capacidades de WM Sprinkler",
        lead: "WM Sprinkler es un sistema avanzado de control de riego que combina la precisión de la automatización con la comodidad de una aplicación web moderna. Fue diseñado para brindar control total del riego tanto de forma local como remota.",
        heroTitle: "Control total del jardín",
        heroBody: "Control de zonas, horarios, automatización climática, monitorización y acceso remoto funcionan en un único sistema coherente. Así, cada riego es preciso y predecible.",
        heroChip: "Multi-zone control",
        heroTagline: "Automation + cloud + real-time",
        cards: [
          {
            title: "Control de múltiples zonas",
            body: "Cada zona puede funcionar de forma independiente y tener su propia configuración.",
            list: ["horario propio", "tiempo de riego individual", "modo manual y automático", "porcentaje dinámico de riego"],
            emphasis: "Puedes ajustar por separado césped, parterres, invernadero y líneas de goteo."
          },
          {
            title: "Horarios avanzados",
            body: "El sistema ofrece planificación flexible y control de secuencias de trabajo por zonas.",
            list: ["horarios diarios y semanales", "múltiples programas en paralelo", "horas de inicio definidas", "días activos según calendario"]
          },
          {
            title: "Corrección climática automática",
            body: "WM Sprinkler analiza las condiciones y ajusta dinámicamente la intensidad del riego.",
            list: ["limitar riego tras precipitaciones", "aumentar dosis con altas temperaturas", "pausar riego durante lluvia", "porcentaje adaptativo de riego"]
          },
          {
            title: "Monitorización en tiempo real",
            body: "En la app tienes una vista en vivo del funcionamiento y del historial del sistema.",
            list: ["zona activa y tiempo restante", "historial de ejecuciones", "porcentaje de corrección automática", "estado de conexión del dispositivo"]
          },
          {
            title: "Acceso local y remoto",
            body: "El sistema funciona de forma local, pero también proporciona acceso seguro por internet.",
            list: ["control remoto desde cualquier lugar", "vista de estado fuera de casa", "notificaciones de eventos", "control total también en vacaciones"]
          },
          {
            title: "Notificaciones e integraciones",
            body: "La plataforma comunica eventos clave y se conecta fácilmente con la automatización del hogar.",
            list: ["inicio y fin de riego", "errores del sistema y cambios de estado", "eventos meteorológicos", "MQTT, Home Assistant y smart-home"]
          },
          {
            title: "Actualizaciones OTA",
            body: "Puedes actualizar el firmware por red sin desmontar el controlador.",
            list: ["nuevas funciones sin visitar el dispositivo", "mejoras de seguridad y estabilidad", "desarrollo continuo sin hardware adicional"]
          },
          {
            title: "Arquitectura modular",
            body: "La base moderna ESP32 aporta flexibilidad y larga vida útil al sistema.",
            list: ["ampliación con sensores", "integración con sistemas smart-home", "escalado del número de zonas", "personalización de ajustes de usuario"]
          }
        ]
      },
      development: {
        title: "Desarrollo de WM Sprinkler",
        lead: "WM Sprinkler es más que un controlador de riego. Es una plataforma moderna de gestión inteligente del riego, diseñada para máxima comodidad, ahorro de agua y automatización total del jardín. Desde la primera línea de código, desarrollamos este sistema como una solución de clase premium que responde a las condiciones ambientales en tiempo real.",
        heroTitle: "Dirección del desarrollo",
        heroBody: "Combinamos automatización del riego, datos meteorológicos, analítica y acceso cloud seguro. El objetivo es un sistema que cada temporada funcione con mayor precisión y ayude realmente a ahorrar agua.",
        heroChip: "AI Water Logic",
        heroTagline: "Smart irrigation platform",
        cards: [
          {
            title: "Ahorro inteligente de agua",
            body: "El sistema analiza datos meteorológicos y decide cuánta agua se necesita realmente.",
            list: ["lluvias", "temperatura", "humedad del aire", "pronósticos del tiempo"],
            emphasis: "Resultado: menores facturas de agua y vegetación más sana, sin intervención manual."
          },
          {
            title: "Automatización que aprende del jardín",
            body: "Desarrollamos AI Water Logic, un algoritmo que analiza el historial de riego y las necesidades de las plantas.",
            list: ["predicción de demanda de agua", "optimización de duración por zona", "consideración de estacionalidad", "sugerencias automáticas de horarios"]
          },
          {
            title: "Control desde cualquier lugar",
            body: "La arquitectura cloud proporciona acceso seguro al sistema siempre que lo necesites.",
            list: ["vista del estado de zonas", "inicio manual del riego", "cambio de horarios", "notificaciones de eventos"]
          },
          {
            title: "Datos que importan",
            body: "Desarrollamos WM Sprinkler como centro de analítica del jardín y ahorro real.",
            list: ["vista del consumo de agua", "estadísticas de riego", "comparaciones mensuales y estacionales", "estimación de ahorros"]
          },
          {
            title: "Interfaz moderna",
            body: "La interfaz se desarrolla con estilo de app smart-home premium.",
            list: ["aplicación web responsiva (PWA)", "soporte para móvil, tablet y ordenador", "paneles de pared dedicados", "vista rápida del estado del sistema"]
          },
          {
            title: "Tecnología preparada para el futuro",
            body: "La plataforma ESP32 ofrece estabilidad, rendimiento y gran potencial de ampliación.",
            list: ["alta potencia de cálculo", "ampliación modular con sensores", "actualizaciones OTA", "durabilidad del proyecto a largo plazo"]
          }
        ],
        roadmapAria: "Hoja de ruta",
        roadmapTitle: "Hoja de ruta del desarrollo",
        roadmapLead: "Plan de desarrollo de WM Sprinkler desde la base del sistema hasta un ecosistema smart-home completo.",
        roadmap: [
          {
            title: "Base del sistema",
            list: ["arquitectura del controlador", "control de zonas en ESP32", "horarios de riego", "panel web local y modos manual/automático"]
          },
          {
            title: "Automatización inteligente",
            list: ["integración de datos meteorológicos", "porcentaje dinámico de riego", "historial de lluvia (rain buffer)", "notificaciones Push/MQTT y Home Assistant"]
          },
          {
            title: "Cloud y acceso remoto",
            list: ["cuentas de usuario", "registro online de dispositivos", "panel cloud y sincronización de ajustes", "OTA remota de firmware"]
          },
          {
            title: "IA y analítica",
            list: ["AI Water Logic", "horarios de autoaprendizaje", "predicción de demanda de agua", "estadísticas e informes avanzados"]
          },
          {
            title: "Ecosistema WM Sprinkler",
            list: ["sensores de humedad del suelo", "sensores de caudal de agua", "módulos de ampliación de zonas", "versión PRO e integraciones Matter/Thread"]
          }
        ]
      },
      about: {
        title: "Sobre nosotros",
        bodyHtml: "WM Sprinkler es un equipo que diseña y desarrolla su propio sistema de riego automático. Nos centramos en soluciones prácticas: funcionamiento estable, implementación sencilla y gestión cómoda tanto local como por internet.<br><br>Contacto: PaweMed, correo: <a href=\"mailto:witkowski.med@gmail.com\">witkowski.med@gmail.com</a>"
      },
      footer: {
        note: "PaweMed 2026"
      }
    }
  };

  function setText(el, value) {
    if (!el || typeof value !== "string") return;
    el.textContent = value;
  }

  function setHtml(el, value) {
    if (!el || typeof value !== "string") return;
    el.innerHTML = value;
  }

  function setTextList(elements, values) {
    if (!Array.isArray(elements) || !Array.isArray(values)) return;
    elements.forEach((el, index) => setText(el, values[index] || ""));
  }

  function setHeadingWithIcon(headingEl, title) {
    if (!headingEl) return;
    const iconEl = headingEl.querySelector(".icon");
    if (!iconEl) {
      setText(headingEl, title);
      return;
    }
    const iconText = iconEl.textContent;
    headingEl.textContent = "";
    const icon = document.createElement("span");
    icon.className = "icon";
    icon.textContent = iconText;
    headingEl.appendChild(icon);
    headingEl.appendChild(document.createTextNode(title || ""));
  }

  function applyCards(cards, cardData) {
    if (!Array.isArray(cards) || !Array.isArray(cardData)) return;
    cards.forEach((card, index) => {
      const data = cardData[index];
      if (!data) return;
      setHeadingWithIcon(card.querySelector("h3"), data.title || "");
      setText(card.querySelector("p:not(.dev-emphasis)"), data.body || "");
      setTextList(Array.from(card.querySelectorAll("ul li")), Array.isArray(data.list) ? data.list : []);
      const emphasisEl = card.querySelector(".dev-emphasis");
      if (emphasisEl) {
        if (data.emphasis) {
          emphasisEl.style.display = "";
          setText(emphasisEl, data.emphasis);
        } else {
          emphasisEl.style.display = "none";
        }
      }
    });
  }

  function applyRoadmap(items, roadmapData) {
    if (!Array.isArray(items) || !Array.isArray(roadmapData)) return;
    items.forEach((item, index) => {
      const data = roadmapData[index];
      if (!data) return;
      setText(item.querySelector(".roadmap-copy h4"), data.title || "");
      setTextList(Array.from(item.querySelectorAll(".roadmap-copy ul li")), Array.isArray(data.list) ? data.list : []);
    });
  }

  function updateLanguageControls(langCode, content) {
    const activeFlag = languageFlags[langCode] || "🇵🇱";
    langTrigger.textContent = activeFlag;
    langTrigger.setAttribute("aria-label", content.ui.langTriggerLabel || "Choose language");
    langTrigger.setAttribute("title", content.ui.langTriggerLabel || "Choose language");
    langMenu.setAttribute("aria-label", content.ui.langMenuLabel || "Language selection");

    langOptions.forEach((btn) => {
      const selected = btn.getAttribute("data-lang") === langCode;
      btn.classList.toggle("is-active", selected);
      btn.setAttribute("aria-checked", selected ? "true" : "false");
    });
  }

  function applyContent(content, langCode) {
    if (!content) return;

    document.documentElement.lang = langCode;
    if (content.meta && typeof content.meta.title === "string") document.title = content.meta.title;
    if (descriptionMeta && content.meta && typeof content.meta.description === "string") {
      descriptionMeta.setAttribute("content", content.meta.description);
    }

    setText(nodes.brandSub, content.ui.brandSub);
    setText(nodes.loginBtn, content.ui.loginBtn);
    setText(nodes.menuLabel, content.ui.menuLabel);
    setTextList(nodes.menuLinks, content.ui.menuLinks);
    if (nodes.heroSection && typeof content.ui.heroAria === "string") {
      nodes.heroSection.setAttribute("aria-label", content.ui.heroAria);
    }

    setText(nodes.heroTitle, content.hero.title);
    setText(nodes.heroBody, content.hero.body);
    setTextList(nodes.heroBadges, content.hero.badges);

    setText(nodes.projectTitle, content.project.title);
    setText(nodes.projectBody, content.project.body);
    setText(nodes.integrationsTitle, content.integrations.title);
    setText(nodes.integrationsBody, content.integrations.body);

    setText(nodes.assumptionsTitle, content.assumptions.title);
    setText(nodes.assumptionsLead, content.assumptions.lead);
    setText(nodes.assumptionsHeroTitle, content.assumptions.heroTitle);
    setText(nodes.assumptionsHeroBody, content.assumptions.heroBody);
    setText(nodes.assumptionsHeroChip, content.assumptions.heroChip);
    setText(nodes.assumptionsHeroTagline, content.assumptions.heroTagline);
    applyCards(nodes.assumptionsCards, content.assumptions.cards);

    setText(nodes.capabilitiesTitle, content.capabilities.title);
    setText(nodes.capabilitiesLead, content.capabilities.lead);
    setText(nodes.capabilitiesHeroTitle, content.capabilities.heroTitle);
    setText(nodes.capabilitiesHeroBody, content.capabilities.heroBody);
    setText(nodes.capabilitiesHeroChip, content.capabilities.heroChip);
    setText(nodes.capabilitiesHeroTagline, content.capabilities.heroTagline);
    applyCards(nodes.capabilitiesCards, content.capabilities.cards);

    setText(nodes.developmentTitle, content.development.title);
    setText(nodes.developmentLead, content.development.lead);
    setText(nodes.developmentHeroTitle, content.development.heroTitle);
    setText(nodes.developmentHeroBody, content.development.heroBody);
    setText(nodes.developmentHeroChip, content.development.heroChip);
    setText(nodes.developmentHeroTagline, content.development.heroTagline);
    applyCards(nodes.developmentCards, content.development.cards);

    if (nodes.roadmapSection && typeof content.development.roadmapAria === "string") {
      nodes.roadmapSection.setAttribute("aria-label", content.development.roadmapAria);
    }
    setText(nodes.roadmapTitle, content.development.roadmapTitle);
    setText(nodes.roadmapLead, content.development.roadmapLead);
    applyRoadmap(nodes.roadmapItems, content.development.roadmap);

    setText(nodes.aboutTitle, content.about.title);
    setHtml(nodes.aboutBody, content.about.bodyHtml);
    setText(nodes.footerNote, content.footer.note);

    updateLanguageControls(langCode, content);
  }

  function getStoredLanguage() {
    const cookieValue = getCookieValue(languageCookieKey).trim().toLowerCase();
    if (supportedLanguages.includes(cookieValue)) return cookieValue;
    try {
      return localStorage.getItem(storageKey) || "";
    } catch (_) {
      return "";
    }
  }

  function storeLanguage(langCode) {
    storeLanguageCookie(langCode);
    try {
      localStorage.setItem(storageKey, langCode);
    } catch (_) {
      // Ignore storage errors.
    }
  }

  function closeMenu() {
    langSwitcher.classList.remove("is-open");
    langTrigger.setAttribute("aria-expanded", "false");
  }

  function openMenu() {
    langSwitcher.classList.add("is-open");
    langTrigger.setAttribute("aria-expanded", "true");
  }

  function setLanguage(langCode, persist, options) {
    const opts = options || {};
    const nextLang = supportedLanguages.includes(langCode) ? langCode : defaultLanguage;
    const content = nextLang === defaultLanguage ? polishContent : translations[nextLang];
    applyContent(content || polishContent, nextLang);
    updateSeoLinks(nextLang);
    if (opts.syncUrl !== false) syncLanguageUrl(nextLang, !!opts.replaceUrl);
    window.dispatchEvent(new Event("resize"));
    if (persist) storeLanguage(nextLang);
  }

  langTrigger.addEventListener("click", (event) => {
    event.preventDefault();
    if (langSwitcher.classList.contains("is-open")) closeMenu();
    else openMenu();
  });

  langOptions.forEach((btn) => {
    btn.addEventListener("click", () => {
      const langCode = btn.getAttribute("data-lang") || "pl";
      setLanguage(langCode, true, { syncUrl: true, replaceUrl: false });
      closeMenu();
      langTrigger.focus();
    });
  });

  document.addEventListener("click", (event) => {
    if (!langSwitcher.contains(event.target)) closeMenu();
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") closeMenu();
  });

  window.addEventListener("popstate", () => {
    const urlLang = languageFromPath(window.location.pathname);
    if (!urlLang) return;
    setLanguage(urlLang, true, { syncUrl: false });
  });

  const urlLang = languageFromPath(window.location.pathname);
  const storedLang = getStoredLanguage();
  const initialLang = urlLang || (supportedLanguages.includes(storedLang) ? storedLang : defaultLanguage);
  setLanguage(initialLang, true, { syncUrl: true, replaceUrl: !urlLang });
})();
