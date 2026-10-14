"""Deterministic Weather gateway fixture; not connected to the guest yet.

The original request/response contract is documented in
tools/legacy_services/fixtures/weather-demo-backend/. No live weather provider is contacted.
"""
from dataclasses import dataclass
from xml.etree import ElementTree as ET

MAX_REQUEST_BYTES = 16384
MAX_CITIES = 16
DATASET = "original-iphone-weather-demo-v1"


@dataclass(frozen=True)
class City:
    identifier: str
    name: str
    region: str
    region_name: str
    temperature: int
    forecast: tuple


CITIES = (
    City("USCA0273|12797509", "Cupertino", "CA", "California", 72,
         ((74, 55), (73, 54), (75, 56), (76, 55), (72, 53), (71, 54))),
    City("USNY0996|2459115", "New York", "NY", "New York", 64,
         ((66, 50), (67, 52), (65, 49), (68, 51), (70, 54), (69, 53))),
)
BY_ID = {city.identifier: city for city in CITIES}


class RequestError(ValueError):
    """Malformed or unsupported request to this bounded demo."""


def _text(element):
    if element is None or len(element):
        raise RequestError("Expected a text element")
    return (element.text or "").strip()


def _one(parent, name):
    matches = parent.findall(name)
    if len(matches) != 1:
        raise RequestError("Expected one " + name)
    return matches[0]


def _query(body):
    if not isinstance(body, bytes) or not 0 < len(body) <= MAX_REQUEST_BYTES:
        raise RequestError("Request size")
    try:
        text = body.decode("utf-8")
    except UnicodeDecodeError as error:
        raise RequestError("Expected UTF-8") from error
    # The original requests have no DTD/entity declarations. Never expand a
    # supplied document's declarations or treat it as a source of external data.
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
        raise RequestError("Document declarations are unsupported")
    try:
        root = ET.fromstring(text)
    except ET.ParseError as error:
        raise RequestError("Malformed XML") from error
    if root.tag != "request" or root.get("api") != "weather" or root.get("apiver") != "1.0.0":
        raise RequestError("Unsupported API")
    query = _one(root, "query")
    if len(root) != 1:
        raise RequestError("Unsupported request elements")
    return query


def _forecast(item, city):
    ET.SubElement(item, "location", id=city.identifier, city=city.name)
    # A declared demo convention: minutes since midnight, with noon between
    # 06:00 and18:00. The original isDay consumer compares integers directly;
    # this is not a claim about the historical server's time units.
    ET.SubElement(item, "astronomy", sunrise="360", sunset="1080",
                  moonfacevisible="0", moonphase="0")
    ET.SubElement(item, "condition", time="720", temp=str(city.temperature), code="32")
    for high, low in city.forecast:
        ET.SubElement(item, "forecast", high=str(high), low=str(low), code="32")


def _location(item, city):
    for tag, value in (("id", city.identifier), ("city", city.name),
                       ("region", city.region), ("regionname", city.region_name),
                       ("country", "US"), ("countryname", "United States")):
        ET.SubElement(item, tag).text = value


def respond(body):
    """Return XML bytes for the two supported original-app request shapes.

    Unknown cities/searches produce an empty list rather than invented data.
    Dataset metadata is explicit; the eventual product must visibly label demo
    mode before routing guest traffic here. Headers/XML alone are not that UI.
    """
    query = _query(body)
    kind = query.get("type")
    if kind == "getforecastbylocationid":
        if query.get("id") != "30" or [node.tag for node in query] != ["list", "language", "unit"]:
            raise RequestError("Unsupported forecast shape")
        if _text(_one(query, "language")) != "en" or _text(_one(query, "unit")) != "f":
            raise RequestError("Only the original en/f request is supported")
        ids = _one(query, "list")
        if not 0 < len(ids) <= MAX_CITIES or any(node.tag != "id" for node in ids):
            raise RequestError("City list bounds")
        requested = list(dict.fromkeys(_text(node) for node in ids))
        if any(not identifier or len(identifier) > 128 for identifier in requested):
            raise RequestError("City identifier bounds")
        cities = [BY_ID[identifier] for identifier in requested if identifier in BY_ID]
        fill = _forecast
    elif kind == "getlocationid":
        if query.get("id") != "3" or [node.tag for node in query] != ["phrase"]:
            raise RequestError("Unsupported search shape")
        phrase = " ".join(_text(_one(query, "phrase")).casefold().split())
        if not 0 < len(phrase) <= 128:
            raise RequestError("Search phrase bounds")
        cities = [city for city in CITIES if phrase in city.name.casefold()
                  or phrase == city.identifier.casefold()]
        fill = _location
    else:
        raise RequestError("Unsupported query")
    root = ET.Element("response", demo="true", dataset=DATASET)
    result = ET.SubElement(root, "result", id=query.get("id"))
    items = ET.SubElement(result, "list")
    for city in cities:
        fill(ET.SubElement(items, "item"), city)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)
