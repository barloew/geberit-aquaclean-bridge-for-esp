"""App, driver, settings and pairing texts in every Homey language (written to locales/ and .homeycompose)."""

APP_NAME = "AquaClean Bridge"

APP_DESC = {
    "en": "Flows for your Geberit AquaClean shower toilet through the AquaClean Bridge for ESP",
    "nl": "Flows voor je Geberit AquaClean douchetoilet via de AquaClean Bridge voor ESP",
    "de": "Flows für dein Geberit AquaClean Dusch-WC über die AquaClean Bridge für ESP",
    "fr": "Des flows pour votre WC lavant Geberit AquaClean via l'AquaClean Bridge pour ESP",
    "it": "Flow per il tuo vaso bidet Geberit AquaClean tramite l'AquaClean Bridge per ESP",
    "sv": "Flöden för din Geberit AquaClean duschtoalett via AquaClean Bridge för ESP",
    "no": "Flows for ditt Geberit AquaClean dusjtoalett via AquaClean Bridge for ESP",
    "es": "Flujos para tu inodoro bidé Geberit AquaClean a través de AquaClean Bridge para ESP",
    "da": "Flows til dit Geberit AquaClean douchetoilet via AquaClean Bridge til ESP",
    "ru": "Сценарии для унитаза-биде Geberit AquaClean через AquaClean Bridge для ESP",
    "pl": "Flow dla toalety myjącej Geberit AquaClean przez AquaClean Bridge dla ESP",
    "ko": "AquaClean Bridge for ESP를 통한 Geberit AquaClean 비데 플로우",
    "ar": "تدفقات لمرحاض الشطف Geberit AquaClean عبر AquaClean Bridge لـ ESP",
}

DRIVER_NAME = {c: "AquaClean Bridge" for c in APP_DESC}

# key -> {lang: text}
S = {
    "settings.connection": {
        "en": "Connection", "nl": "Verbinding", "de": "Verbindung", "fr": "Connexion", "it": "Connessione",
        "sv": "Anslutning", "no": "Tilkobling", "es": "Conexión", "da": "Forbindelse", "ru": "Подключение",
        "pl": "Połączenie", "ko": "연결", "ar": "الاتصال"},
    "settings.info": {
        "en": "Bridge", "nl": "Bridge", "de": "Bridge", "fr": "Bridge", "it": "Bridge", "sv": "Bridge",
        "no": "Bridge", "es": "Bridge", "da": "Bridge", "ru": "Bridge", "pl": "Bridge", "ko": "브리지", "ar": "الجسر"},
    "settings.host": {
        "en": "Host name or IP address", "nl": "Hostnaam of IP-adres", "de": "Hostname oder IP-Adresse",
        "fr": "Nom d'hôte ou adresse IP", "it": "Nome host o indirizzo IP", "sv": "Värdnamn eller IP-adress",
        "no": "Vertsnavn eller IP-adresse", "es": "Nombre de host o dirección IP", "da": "Værtsnavn eller IP-adresse",
        "ru": "Имя хоста или IP-адрес", "pl": "Nazwa hosta lub adres IP", "ko": "호스트 이름 또는 IP 주소",
        "ar": "اسم المضيف أو عنوان IP"},
    "settings.port": {
        "en": "Port", "nl": "Poort", "de": "Port", "fr": "Port", "it": "Porta", "sv": "Port", "no": "Port",
        "es": "Puerto", "da": "Port", "ru": "Порт", "pl": "Port", "ko": "포트", "ar": "المنفذ"},
    "settings.key": {
        "en": "API encryption key", "nl": "API-encryptiesleutel", "de": "API-Verschlüsselungsschlüssel",
        "fr": "Clé de chiffrement de l'API", "it": "Chiave di crittografia API", "sv": "API-krypteringsnyckel",
        "no": "API-krypteringsnøkkel", "es": "Clave de cifrado de la API", "da": "API-krypteringsnøgle",
        "ru": "Ключ шифрования API", "pl": "Klucz szyfrowania API", "ko": "API 암호화 키", "ar": "مفتاح تشفير API"},
    "settings.firmware": {
        "en": "Firmware", "nl": "Firmware", "de": "Firmware", "fr": "Firmware", "it": "Firmware", "sv": "Firmware",
        "no": "Fastvare", "es": "Firmware", "da": "Firmware", "ru": "Прошивка", "pl": "Oprogramowanie",
        "ko": "펌웨어", "ar": "البرنامج الثابت"},
    "settings.entities": {
        "en": "Recognised entities", "nl": "Herkende entiteiten", "de": "Erkannte Entitäten",
        "fr": "Entités reconnues", "it": "Entità riconosciute", "sv": "Igenkända entiteter",
        "no": "Gjenkjente entiteter", "es": "Entidades reconocidas", "da": "Genkendte entiteter",
        "ru": "Распознанные сущности", "pl": "Rozpoznane encje", "ko": "인식된 엔티티", "ar": "الكيانات المعروفة"},
    # pairing (locales)
    "pair.title": {
        "en": "Connect the AquaClean Bridge", "nl": "AquaClean Bridge verbinden", "de": "AquaClean Bridge verbinden",
        "fr": "Connecter l'AquaClean Bridge", "it": "Collega l'AquaClean Bridge", "sv": "Anslut AquaClean Bridge",
        "no": "Koble til AquaClean Bridge", "es": "Conectar AquaClean Bridge", "da": "Forbind AquaClean Bridge",
        "ru": "Подключение AquaClean Bridge", "pl": "Połącz AquaClean Bridge", "ko": "AquaClean Bridge 연결",
        "ar": "توصيل AquaClean Bridge"},
    "pair.intro": {
        "en": "Use the key you saved on the bridge's /setup page (native API route).",
        "nl": "Gebruik de sleutel die je op de /setup-pagina van de bridge hebt opgeslagen (route native API).",
        "de": "Verwende den Schlüssel, den du auf der /setup-Seite der Bridge gespeichert hast (Route Native API).",
        "fr": "Utilisez la clé enregistrée sur la page /setup du bridge (route API native).",
        "it": "Usa la chiave salvata nella pagina /setup del bridge (route API nativa).",
        "sv": "Använd nyckeln du sparade på bridgens /setup-sida (native API-vägen).",
        "no": "Bruk nøkkelen du lagret på bridgens /setup-side (native API-ruten).",
        "es": "Usa la clave que guardaste en la página /setup del bridge (ruta API nativa).",
        "da": "Brug nøglen, du gemte på bridgens /setup-side (native API-ruten).",
        "ru": "Используйте ключ, сохранённый на странице /setup моста (маршрут native API).",
        "pl": "Użyj klucza zapisanego na stronie /setup mostka (trasa native API).",
        "ko": "브리지의 /setup 페이지에 저장한 키를 사용하세요(네이티브 API 경로).",
        "ar": "استخدم المفتاح الذي حفظته في صفحة ‎/setup الخاصة بالجسر (مسار native API)."},
    "pair.found": {
        "en": "Found on the network", "nl": "Gevonden in het netwerk", "de": "Im Netzwerk gefunden",
        "fr": "Trouvé sur le réseau", "it": "Trovato in rete", "sv": "Hittad i nätverket", "no": "Funnet i nettverket",
        "es": "Encontrado en la red", "da": "Fundet på netværket", "ru": "Найдено в сети", "pl": "Znalezione w sieci",
        "ko": "네트워크에서 찾음", "ar": "تم العثور عليه في الشبكة"},
    "pair.manual": {
        "en": "Enter manually", "nl": "Handmatig invoeren", "de": "Manuell eingeben", "fr": "Saisir manuellement",
        "it": "Inserisci manualmente", "sv": "Ange manuellt", "no": "Angi manuelt", "es": "Introducir manualmente",
        "da": "Indtast manuelt", "ru": "Ввести вручную", "pl": "Wprowadź ręcznie", "ko": "직접 입력",
        "ar": "إدخال يدوي"},
    "pair.key_hint": {
        "en": "44 characters, ends with '='. Leave empty only for a bridge without a key.",
        "nl": "44 tekens, eindigt op '='. Alleen leeg laten bij een bridge zonder sleutel.",
        "de": "44 Zeichen, endet auf '='. Nur leer lassen bei einer Bridge ohne Schlüssel.",
        "fr": "44 caractères, se termine par « = ». Laisser vide seulement pour un bridge sans clé.",
        "it": "44 caratteri, termina con '='. Lascia vuoto solo per un bridge senza chiave.",
        "sv": "44 tecken, slutar med '='. Lämna tomt endast för en bridge utan nyckel.",
        "no": "44 tegn, slutter med '='. La stå tomt bare for en bridge uten nøkkel.",
        "es": "44 caracteres, termina en '='. Déjalo vacío solo si el bridge no tiene clave.",
        "da": "44 tegn, slutter med '='. Lad kun feltet stå tomt for en bridge uden nøgle.",
        "ru": "44 символа, заканчивается на «=». Оставьте пустым, только если у моста нет ключа.",
        "pl": "44 znaki, kończy się na „=”. Zostaw puste tylko dla mostka bez klucza.",
        "ko": "44자, '='로 끝남. 키가 없는 브리지에만 비워 두세요.",
        "ar": "44 حرفًا وينتهي بـ '='. اتركه فارغًا فقط لجسر بدون مفتاح."},
    "pair.connect": {
        "en": "Connect", "nl": "Verbinden", "de": "Verbinden", "fr": "Connecter", "it": "Collega", "sv": "Anslut",
        "no": "Koble til", "es": "Conectar", "da": "Forbind", "ru": "Подключить", "pl": "Połącz", "ko": "연결",
        "ar": "اتصال"},
    "pair.error_host": {
        "en": "Enter a host name or IP address.", "nl": "Vul een hostnaam of IP-adres in.",
        "de": "Gib einen Hostnamen oder eine IP-Adresse ein.", "fr": "Saisissez un nom d'hôte ou une adresse IP.",
        "it": "Inserisci un nome host o un indirizzo IP.", "sv": "Ange ett värdnamn eller en IP-adress.",
        "no": "Angi et vertsnavn eller en IP-adresse.", "es": "Introduce un nombre de host o una dirección IP.",
        "da": "Indtast et værtsnavn eller en IP-adresse.", "ru": "Введите имя хоста или IP-адрес.",
        "pl": "Wpisz nazwę hosta lub adres IP.", "ko": "호스트 이름 또는 IP 주소를 입력하세요.",
        "ar": "أدخل اسم مضيف أو عنوان IP."},
    "pair.error_key": {
        "en": "The bridge refused the encryption key.", "nl": "De bridge weigert de encryptiesleutel.",
        "de": "Die Bridge lehnt den Verschlüsselungsschlüssel ab.", "fr": "Le bridge refuse la clé de chiffrement.",
        "it": "Il bridge rifiuta la chiave di crittografia.", "sv": "Bridgen godtar inte krypteringsnyckeln.",
        "no": "Bridgen avviser krypteringsnøkkelen.", "es": "El bridge rechaza la clave de cifrado.",
        "da": "Bridgen afviser krypteringsnøglen.", "ru": "Мост отклонил ключ шифрования.",
        "pl": "Mostek odrzucił klucz szyfrowania.", "ko": "브리지가 암호화 키를 거부했습니다.",
        "ar": "رفض الجسر مفتاح التشفير."},
    "pair.error_connect": {
        "en": "No connection: __error__", "nl": "Geen verbinding: __error__", "de": "Keine Verbindung: __error__",
        "fr": "Pas de connexion : __error__", "it": "Nessuna connessione: __error__",
        "sv": "Ingen anslutning: __error__", "no": "Ingen tilkobling: __error__", "es": "Sin conexión: __error__",
        "da": "Ingen forbindelse: __error__", "ru": "Нет соединения: __error__", "pl": "Brak połączenia: __error__",
        "ko": "연결 안 됨: __error__", "ar": "لا يوجد اتصال: __error__"},
    "pair.error_not_bridge": {
        "en": "This ESPHome device is not an AquaClean Bridge.",
        "nl": "Dit ESPHome-apparaat is geen AquaClean Bridge.",
        "de": "Dieses ESPHome-Gerät ist keine AquaClean Bridge.",
        "fr": "Cet appareil ESPHome n'est pas un AquaClean Bridge.",
        "it": "Questo dispositivo ESPHome non è un AquaClean Bridge.",
        "sv": "Den här ESPHome-enheten är ingen AquaClean Bridge.",
        "no": "Denne ESPHome-enheten er ikke en AquaClean Bridge.",
        "es": "Este dispositivo ESPHome no es un AquaClean Bridge.",
        "da": "Denne ESPHome-enhed er ikke en AquaClean Bridge.",
        "ru": "Это устройство ESPHome не является AquaClean Bridge.",
        "pl": "To urządzenie ESPHome nie jest AquaClean Bridge.",
        "ko": "이 ESPHome 장치는 AquaClean Bridge가 아닙니다.",
        "ar": "جهاز ESPHome هذا ليس AquaClean Bridge."},
    "device.connecting": {
        "en": "Connecting to the bridge…", "nl": "Verbinden met de bridge…", "de": "Verbinde mit der Bridge…",
        "fr": "Connexion au bridge…", "it": "Connessione al bridge…", "sv": "Ansluter till bridgen…",
        "no": "Kobler til bridgen…", "es": "Conectando con el bridge…", "da": "Forbinder til bridgen…",
        "ru": "Подключение к мосту…", "pl": "Łączenie z mostkiem…", "ko": "브리지에 연결 중…",
        "ar": "جارٍ الاتصال بالجسر…"},
    "device.disconnected": {
        "en": "Bridge not reachable", "nl": "Bridge niet bereikbaar", "de": "Bridge nicht erreichbar",
        "fr": "Bridge injoignable", "it": "Bridge non raggiungibile", "sv": "Bridgen kan inte nås",
        "no": "Bridgen kan ikke nås", "es": "Bridge no accesible", "da": "Bridgen kan ikke nås",
        "ru": "Мост недоступен", "pl": "Mostek nieosiągalny", "ko": "브리지에 연결할 수 없음",
        "ar": "تعذر الوصول إلى الجسر"},
}
# the pairing form reuses the settings labels
S["pair.host"] = S["settings.host"]
S["pair.port"] = S["settings.port"]
S["pair.key"] = S["settings.key"]
