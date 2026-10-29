// Reads a WireGuard configuration file: the ordinary `.conf` that wg-quick
// takes, uploaded to the board from the setup-mode page (spec 027).
//
// Nothing of ESP-IDF in here, so the host can compile and test it
// (tests/test_wg_conf.py in the Sofia repository). The messages are read by
// the person loading the file, so they are in Spanish; none of them ever
// carries a key.
#pragma once

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace sofia {

struct WgConf {
    std::string private_key;       // [Interface] PrivateKey
    std::string address;           // [Interface] Address: the board inside the tunnel, no mask
    std::string peer_public_key;   // [Peer] PublicKey
    std::string preshared_key;     // [Peer] PresharedKey, optional
    std::string endpoint_host;     // [Peer] Endpoint
    int endpoint_port = 51820;
    std::string peer_address;      // the server inside the tunnel
    int keepalive = 25;            // keeps the NAT of whoever lends the internet open
};

namespace wgconf {

inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

inline std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) {
        item = Trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

// A WireGuard key: 32 bytes in base64, so 43 characters and one '='.
inline bool IsKey(const std::string& s) {
    if (s.size() != 44 || s[43] != '=') return false;
    for (size_t i = 0; i < 43; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (!std::isalnum(c) && c != '+' && c != '/') return false;
    }
    return true;
}

// "10.77.105.2" out of "10.77.105.2/24"; empty unless it is a dotted IPv4.
inline std::string Ipv4(const std::string& s, int* prefix = nullptr) {
    const size_t slash = s.find('/');
    const std::string host = Trim(s.substr(0, slash));
    int dots = 0, value = -1;
    for (const char c : host) {
        if (c == '.') {
            if (value < 0 || value > 255) return "";
            ++dots; value = -1;
        } else if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value * 10) + (c - '0');
            if (value > 255) return "";
        } else {
            return "";
        }
    }
    if (dots != 3 || value < 0) return "";
    if (prefix != nullptr) *prefix = slash == std::string::npos ? 32 : std::atoi(s.c_str() + slash + 1);
    return host;
}

}  // namespace wgconf

inline bool ParseWgConf(const std::string& text, WgConf& out, std::string& error) {
    using namespace wgconf;
    out = WgConf{};
    std::string section, address_field, allowed_field, endpoint_field;
    bool saw_interface = false;
    int peers = 0;
    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.erase(comment);
        line = Trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            if (section == "interface") saw_interface = true;
            if (section == "peer") ++peers;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        if (section == "interface") {
            if (key == "privatekey") out.private_key = value;
            else if (key == "address") address_field = value;
        } else if (section == "peer" && peers == 1) {
            if (key == "publickey") out.peer_public_key = value;
            else if (key == "presharedkey") out.preshared_key = value;
            else if (key == "endpoint") endpoint_field = value;
            else if (key == "allowedips") allowed_field = value;
            else if (key == "persistentkeepalive") out.keepalive = std::atoi(value.c_str());
        }
    }

    if (!saw_interface) { error = "No parece un fichero de WireGuard: falta la sección [Interface]."; return false; }
    if (peers == 0) { error = "Falta la sección [Peer], que es la casa."; return false; }
    if (peers > 1) { error = "Hay más de una sección [Peer]: este fichero tiene que tener una sola, la de casa."; return false; }
    if (out.private_key.empty()) { error = "Falta PrivateKey en [Interface]."; return false; }
    if (!IsKey(out.private_key)) { error = "PrivateKey no tiene forma de clave de WireGuard."; return false; }
    if (out.peer_public_key.empty()) { error = "Falta PublicKey en [Peer]."; return false; }
    if (!IsKey(out.peer_public_key)) { error = "PublicKey no tiene forma de clave de WireGuard."; return false; }
    if (!out.preshared_key.empty() && !IsKey(out.preshared_key)) { error = "PresharedKey no tiene forma de clave de WireGuard."; return false; }

    if (address_field.empty()) { error = "Falta Address en [Interface]."; return false; }
    for (const auto& item : Split(address_field, ',')) {
        out.address = Ipv4(item);
        if (!out.address.empty()) break;
    }
    if (out.address.empty()) { error = "Address tiene que traer una dirección IPv4, como 10.77.105.2/24."; return false; }

    if (endpoint_field.empty()) { error = "Falta Endpoint en [Peer]: el nombre de casa y su puerto."; return false; }
    if (endpoint_field.front() == '[') { error = "Endpoint con dirección IPv6 no se puede usar: poné un nombre o una IPv4."; return false; }
    const size_t colon = endpoint_field.rfind(':');
    out.endpoint_host = Trim(endpoint_field.substr(0, colon));
    if (colon != std::string::npos) {
        const std::string port = Trim(endpoint_field.substr(colon + 1));
        out.endpoint_port = port.empty() || port.size() > 5 ? 0 : std::atoi(port.c_str());
    }
    if (out.endpoint_host.empty() || out.endpoint_host.find(' ') != std::string::npos ||
        out.endpoint_port < 1 || out.endpoint_port > 65535) {
        error = "Endpoint tiene que ser nombre:puerto, como casa.example.org:51877.";
        return false;
    }
    if (out.keepalive < 1 || out.keepalive > 600) out.keepalive = 25;

    // The server inside the tunnel: the one host among AllowedIPs; without it,
    // the first address of the board's own tunnel network.
    for (const auto& item : Split(allowed_field, ',')) {
        int prefix = 0;
        const std::string host = Ipv4(item, &prefix);
        if (!host.empty() && prefix == 32 && host != "0.0.0.0") { out.peer_address = host; break; }
    }
    if (out.peer_address.empty()) {
        out.peer_address = out.address.substr(0, out.address.rfind('.')) + ".1";
    }
    return true;
}

}  // namespace sofia
