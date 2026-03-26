const CACHE_NAME = "sprinkler-app-v7";
const CORE_ASSETS = [
  "/",
  "/index.html",
  "/manifest.json",
  "/sw.js",
  "/assets/index-CQLRYLcA.js",
  "/assets/index-DzUbrOvj.css",
  "/assets/logo-optimized-BnrUG8xa.png",
];

// Install event
self.addEventListener("install", (event) => {
  event.waitUntil(
    caches
      .open(CACHE_NAME)
      .then((cache) => cache.addAll(CORE_ASSETS))
      .then(() => self.skipWaiting())
  );
});

// Fetch event
self.addEventListener("fetch", (event) => {
  const { request } = event;

  if (request.method !== "GET") return;

  const url = new URL(request.url);
  if (url.origin !== self.location.origin) return;

  if (url.pathname.startsWith("/api/")) {
    event.respondWith(
      fetch(request).catch(
        () =>
          new Response(JSON.stringify({ error: "offline" }), {
            status: 503,
            headers: { "Content-Type": "application/json" },
          })
      )
    );
    return;
  }

  if (request.mode === "navigate") {
    event.respondWith(
      fetch(request)
        .then((response) => {
          const copy = response.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(request, copy));
          return response;
        })
        .catch(async () => {
          const cachedPage = await caches.match(request);
          if (cachedPage) return cachedPage;
          return caches.match("/index.html");
        })
    );
    return;
  }

  const staticDestinations = new Set([
    "script",
    "style",
    "image",
    "font",
    "manifest",
  ]);
  if (staticDestinations.has(request.destination)) {
    event.respondWith(
      fetch(request)
        .then((response) => {
          if (response && response.status === 200 && response.type === "basic") {
            const copy = response.clone();
            caches.open(CACHE_NAME).then((cache) => cache.put(request, copy));
          }
          return response;
        })
        .catch(async () => {
          const cached = await caches.match(request);
          return cached || caches.match("/index.html");
        })
    );
    return;
  }

  event.respondWith(
    fetch(request).catch(async () => {
      const cached = await caches.match(request);
      return cached || caches.match("/index.html");
    })
  );
});

// Activate event
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((cacheNames) =>
        Promise.all(
          cacheNames.map((cacheName) => {
            if (cacheName !== CACHE_NAME) return caches.delete(cacheName);
            return Promise.resolve();
          })
        )
      )
      .then(() => self.clients.claim())
  );
});

// Background sync for offline operations
self.addEventListener("sync", (event) => {
  if (event.tag === "zone-control") {
    event.waitUntil(syncZoneOperations());
  }
});

async function syncZoneOperations() {
  // Get pending operations from IndexedDB and sync with server
  try {
    const pendingOperations = await getPendingOperations();
    for (const operation of pendingOperations) {
      await fetch("/api" + operation.endpoint, {
        method: operation.method,
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(operation.data),
      });
      await removePendingOperation(operation.id);
    }
  } catch (error) {
    console.error("Background sync failed:", error);
  }
}

// Mock functions for offline storage (would use IndexedDB in production)
async function getPendingOperations() {
  return [];
}

async function removePendingOperation(id) {
  // Remove from IndexedDB
}

// Push notifications for zone status
self.addEventListener("push", (event) => {
  const options = {
    body: event.data ? event.data.text() : "Sprawdź status nawadniania",
    icon: "/assets/logo-optimized-BnrUG8xa.png",
    badge: "/assets/logo-optimized-BnrUG8xa.png",
    tag: "sprinkler-notification",
    requireInteraction: true,
    actions: [
      {
        action: "open-zones",
        title: "Otwórz strefy",
      },
      {
        action: "dismiss",
        title: "Zamknij",
      },
    ],
  };

  event.waitUntil(self.registration.showNotification("System Nawadniania", options));
});

// Handle notification clicks
self.addEventListener("notificationclick", (event) => {
  event.notification.close();

  if (event.action === "open-zones") {
    event.waitUntil(clients.openWindow("/?section=zones"));
  } else {
    event.waitUntil(clients.openWindow("/"));
  }
});
