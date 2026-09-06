"""Backend contract checks, not original guest-parser acceptance."""
import http.client
from http.server import HTTPServer
from pathlib import Path
import threading
import unittest
from xml.etree import ElementTree as ET

from tools.legacy_services.server import Handler
from tools.legacy_services.weather import MAX_REQUEST_BYTES, RequestError, respond

FIXTURES = Path(__file__).with_name("fixtures")
FORECAST = (FIXTURES / "forecast-request.xml").read_bytes()
SEARCH = (FIXTURES / "search-request.xml").read_bytes()


class WeatherContract(unittest.TestCase):
    def test_original_forecast_shape_and_fixed_values(self):
        response = respond(FORECAST)
        self.assertEqual(response, respond(FORECAST))
        root = ET.fromstring(response)
        self.assertEqual(root.tag, "response")
        self.assertEqual(root.get("demo"), "true")
        items = root.findall("./result/list/item")
        self.assertEqual(len(items), 2)
        self.assertEqual([item.find("location").get("id") for item in items],
                         ["USCA0273|12797509", "USNY0996|2459115"])
        self.assertEqual([item.find("condition").get("temp") for item in items], ["72", "64"])
        for item in items:
            self.assertEqual(len(item.findall("forecast")), 6)
            astronomy, condition = item.find("astronomy"), item.find("condition")
            self.assertLess(int(astronomy.get("sunrise")), int(condition.get("time")))
            self.assertLess(int(condition.get("time")), int(astronomy.get("sunset")))
            for day in item.findall("forecast"):
                self.assertGreaterEqual(int(day.get("high")), int(day.get("low")))
                self.assertIsNotNone(day.get("code"))

    def test_search_uses_text_fields_and_forecast_identity(self):
        item = ET.fromstring(respond(SEARCH)).find("./result/list/item")
        self.assertEqual({child.tag: child.text for child in item}, {
            "id": "USCA0273|12797509", "city": "Cupertino", "region": "CA",
            "regionname": "California", "country": "US", "countryname": "United States",
        })
        self.assertTrue(all(not child.attrib for child in item))
        body = FORECAST.replace(b"USNY0996|2459115", item.findtext("id").encode())
        selected = ET.fromstring(respond(body)).findall("./result/list/item")
        self.assertEqual(len(selected), 1)  # Repeated IDs do not duplicate a city.
        self.assertEqual(selected[0].find("location").get("id"), item.findtext("id"))

    def test_search_normalization_and_no_fabricated_results(self):
        body = SEARCH.replace(b"Cupertino", b"  nEw   YoRk  ")
        self.assertEqual(ET.fromstring(respond(body)).findtext("./result/list/item/city"), "New York")
        for phrase in [b"Nowhere", b"&lt;city&gt;", b"A&amp;B"]:
            with self.subTest(phrase=phrase):
                self.assertEqual(ET.fromstring(respond(SEARCH.replace(b"Cupertino", phrase))).findall("./result/list/item"), [])

    def test_unknown_id_is_not_replaced_by_a_default_city(self):
        body = FORECAST.replace(b"USCA0273|12797509", b"unknown").replace(b"USNY0996|2459115", b"absent")
        self.assertEqual(ET.fromstring(respond(body)).findall("./result/list/item"), [])

    def test_malformed_and_unsupported_inputs(self):
        cases = [b"", b"x" * (MAX_REQUEST_BYTES + 1), b"\xff", b"<request>",
                 FORECAST.replace(b'api="weather"', b'api="stocks"'),
                 FORECAST.replace(b'apiver="1.0.0"', b'apiver="2"'),
                 FORECAST.replace(b'<unit>f</unit>', b'<unit>c</unit>'),
                 FORECAST.replace(b'<language>en</language>', b'<language>fr</language>'),
                 FORECAST.replace(b'getforecastbylocationid', b'unknown'),
                 FORECAST.replace(b'id="30"', b'id="3"'),
                 SEARCH.replace(b"<phrase>Cupertino</phrase>", b"<phrase/>"),
                 SEARCH.replace(b"Cupertino", b"x" * 129),
                 SEARCH.replace(b"Cupertino", b"<child/>"),
                 SEARCH.replace(b"</query>", b"<phrase>New York</phrase></query>"),
                 SEARCH.replace(b"</request>", b"<query/></request>"),
                 b'<!DOCTYPE request [<!ENTITY x "Cupertino">]>' + SEARCH.split(b"?>", 1)[1].replace(b"Cupertino", b"&x;")]
        for body in cases:
            with self.subTest(body=body[:100]), self.assertRaises(RequestError):
                respond(body)

    def test_city_count_and_identifier_bounds(self):
        root = ET.fromstring(FORECAST)
        cities = root.find("./query/list")
        cities.clear()
        for _ in range(17):
            ET.SubElement(cities, "id").text = "USCA0273|12797509"
        with self.assertRaises(RequestError):
            respond(ET.tostring(root))
        with self.assertRaises(RequestError):
            respond(FORECAST.replace(b"USCA0273|12797509", b""))


class WeatherHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = HTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)

    def request(self, body=FORECAST, path="/dgw?imei=synthetic-test-only&apptype=weather&t=0", headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        try:
            connection.request("POST", path, body, headers or {"Content-Type": "text/xml"})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_actual_http_response(self):
        status, headers, body = self.request()
        self.assertEqual(status, 200)
        self.assertEqual(body, respond(FORECAST))
        self.assertEqual(int(headers["Content-Length"]), len(body))
        self.assertEqual(headers["X-IPhone-Demo"], "original-iphone-weather-demo-v1")
        self.assertEqual(headers["Cache-Control"], "no-store")
        self.assertNotIn(b"synthetic-test-only", body)

    def test_http_failure_statuses(self):
        self.assertEqual(self.request(path="/unknown")[0], 404)
        self.assertEqual(self.request(headers={"Content-Type": "application/json"})[0], 415)
        self.assertEqual(self.request(body=b"<broken>")[0], 400)
        self.assertEqual(self.request(body=b"x" * (MAX_REQUEST_BYTES + 1))[0], 413)

    def test_invalid_url_and_transfer_encoding(self):
        # Bypass http.client's own URL validation so the malformed target
        # actually reaches the server under test.
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        try:
            connection.putrequest("POST", "http://[broken", skip_host=True)
            connection.putheader("Host", "localhost")
            connection.putheader("Content-Length", "0")
            connection.endheaders()
            response = connection.getresponse()
            self.assertEqual(response.status, 400)
            response.read()
        finally:
            connection.close()
        self.assertEqual(self.request(headers={"Content-Type": "text/xml", "Transfer-Encoding": "chunked"})[0], 400)

    def test_ambiguous_http_framing_is_rejected(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        try:
            connection.putrequest("POST", "/dgw")
            connection.putheader("Content-Type", "text/xml")
            connection.putheader("Content-Length", "0")
            connection.putheader("Content-Length", str(len(FORECAST)))
            connection.endheaders()
            response = connection.getresponse()
            self.assertEqual(response.status, 400)
            response.read()
        finally:
            connection.close()


if __name__ == "__main__":
    unittest.main()
