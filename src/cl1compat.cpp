/*
 * cl1compat.cpp - ClanLib 1.0 compatibility layer on top of ClanLib 4.x
 * See cl1compat.h
 */
#include "cl1compat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>

// If the cars seem to turn the wrong way (the frames of the rotated car
// images are in the wrong order), change this to -1.
#ifndef CL1_ROTATE_SIGN
#define CL1_ROTATE_SIGN 1
#endif

// ===========================================================================
// Small helpers
// ===========================================================================

static std::string cl1_lower(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (char)std::tolower((unsigned char)s[i]);
    return s;
}

static std::string cl1_extension(const std::string& path)
{
    size_t dot = path.rfind('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    return cl1_lower(path.substr(dot + 1));
}

static std::string cl1_dirname(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash + 1);
}

static bool cl1_is_absolute(const std::string& path)
{
    return !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                             (path.size() > 1 && path[1] == ':'));
}

// ===========================================================================
// Mini XML parser (enough for resources.xml)
// ===========================================================================

struct CL1XmlNode
{
    std::string name;                               // element name
    std::map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<CL1XmlNode> > children;

    bool has(const std::string& key) const { return attributes.find(key) != attributes.end(); }
    std::string get(const std::string& key, const std::string& def = "") const
    {
        std::map<std::string, std::string>::const_iterator it = attributes.find(key);
        return it == attributes.end() ? def : it->second;
    }
    const CL1XmlNode* child(const std::string& elementName) const
    {
        for (size_t i = 0; i < children.size(); ++i)
            if (children[i]->name == elementName) return children[i].get();
        return 0;
    }
};

namespace {

std::string xmlDecode(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] != '&') { out += in[i]; continue; }
        size_t semi = in.find(';', i);
        if (semi == std::string::npos) { out += in[i]; continue; }
        std::string ent = in.substr(i + 1, semi - i - 1);
        if      (ent == "quot") out += '"';
        else if (ent == "amp")  out += '&';
        else if (ent == "lt")   out += '<';
        else if (ent == "gt")   out += '>';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#')
        {
            int code = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                           ? (int)std::strtol(ent.c_str() + 2, 0, 16)
                           : (int)std::strtol(ent.c_str() + 1, 0, 10);
            if (code > 0 && code < 128) out += (char)code;
        }
        else { out += in.substr(i, semi - i + 1); }
        i = semi;
    }
    return out;
}

struct XmlParser
{
    const std::string& s;
    size_t p;
    XmlParser(const std::string& text) : s(text), p(0) {}

    void skipSpace() { while (p < s.size() && std::isspace((unsigned char)s[p])) ++p; }

    bool startsWith(const char* t) const { return s.compare(p, std::strlen(t), t) == 0; }

    // Skips text, comments, processing instructions and doctype until the next element tag.
    bool nextTag()
    {
        while (p < s.size())
        {
            if (s[p] != '<') { ++p; continue; }
            if (startsWith("<!--")) { size_t e = s.find("-->", p + 4); p = (e == std::string::npos) ? s.size() : e + 3; continue; }
            if (startsWith("<?"))   { size_t e = s.find("?>", p + 2);  p = (e == std::string::npos) ? s.size() : e + 2; continue; }
            if (startsWith("<!"))   { size_t e = s.find('>', p + 2);   p = (e == std::string::npos) ? s.size() : e + 1; continue; }
            return true;
        }
        return false;
    }

    std::string readName()
    {
        size_t b = p;
        while (p < s.size() && !std::isspace((unsigned char)s[p]) && s[p] != '>' && s[p] != '/' && s[p] != '=') ++p;
        return s.substr(b, p - b);
    }

    // Parses one element (p is at '<'). Returns 0 on error.
    CL1XmlNode* parseElement()
    {
        if (p >= s.size() || s[p] != '<') return 0;
        ++p;
        std::unique_ptr<CL1XmlNode> node(new CL1XmlNode);
        node->name = readName();
        // attributes
        for (;;)
        {
            skipSpace();
            if (p >= s.size()) return 0;
            if (s[p] == '/')            // self closing
            {
                p += 2;                 // "/>"
                return node.release();
            }
            if (s[p] == '>') { ++p; break; }
            std::string key = readName();
            skipSpace();
            if (p < s.size() && s[p] == '=')
            {
                ++p;
                skipSpace();
                if (p >= s.size()) return 0;
                char quote = s[p];
                if (quote != '"' && quote != '\'') return 0;
                size_t e = s.find(quote, p + 1);
                if (e == std::string::npos) return 0;
                node->attributes[key] = xmlDecode(s.substr(p + 1, e - p - 1));
                p = e + 1;
            }
            else if (key.empty()) return 0;
        }
        // children until the closing tag
        for (;;)
        {
            if (!nextTag()) return 0;
            if (p + 1 < s.size() && s[p + 1] == '/')    // closing tag
            {
                size_t e = s.find('>', p);
                if (e == std::string::npos) return 0;
                p = e + 1;
                return node.release();
            }
            CL1XmlNode* c = parseElement();
            if (!c) return 0;
            node->children.push_back(std::unique_ptr<CL1XmlNode>(c));
        }
    }
};

} // namespace

CL_ResourceManager::CL_ResourceManager(const std::string& xmlFile)
{
    std::ifstream in(xmlFile.c_str(), std::ios::binary);
    if (!in)
        throw CL_Error("IODeviceProvider_File::IODeviceProvider_File(): Unable to open file '" + xmlFile + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();

    XmlParser parser(text);
    if (!parser.nextTag())
        throw CL_Error("Resource file '" + xmlFile + "' contains no data");
    root.reset(parser.parseElement());
    if (!root)
        throw CL_Error("Resource file '" + xmlFile + "' could not be parsed");

    baseDir = cl1_dirname(xmlFile);
}

CL_ResourceManager::~CL_ResourceManager() {}

const CL1XmlNode* CL_ResourceManager::find(const std::string& path) const
{
    if (!root) return 0;

    std::vector<std::string> parts;
    {
        std::string cur;
        for (size_t i = 0; i < path.size(); ++i)
        {
            if (path[i] == '/') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
            else cur += path[i];
        }
        if (!cur.empty()) parts.push_back(cur);
    }
    if (parts.empty()) return 0;

    const CL1XmlNode* node = root.get();
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const CL1XmlNode* next = 0;
        bool last = (i + 1 == parts.size());
        for (size_t c = 0; c < node->children.size(); ++c)
        {
            const CL1XmlNode* ch = node->children[c].get();
            if (ch->get("name") != parts[i]) continue;
            if (!last && ch->name != "section") continue;
            next = ch;
            break;
        }
        if (!next) return 0;
        node = next;
    }
    return node;
}

static const CL1XmlNode* cl1_require(const CL_ResourceManager* res, const std::string& path)
{
    if (!res) throw CL_Error("No resource manager given for resource '" + path + "'");
    const CL1XmlNode* n = res->find(path);
    if (!n) throw CL_Error("Resource '" + path + "' not found");
    return n;
}

static std::string cl1_resource_file(const CL_ResourceManager* res, const std::string& file)
{
    if (cl1_is_absolute(file)) return file;
    return res->base_dir() + file;
}

CL_Integer::CL_Integer(const std::string& path, CL_ResourceManager* resources) : value(0)
{
    const CL1XmlNode* n = cl1_require(resources, path);
    if (!n->has("value")) throw CL_Error("Resource '" + path + "' has no value");
    value = std::atoi(n->get("value").c_str());
}

std::string CL_String::load(const std::string& path, CL_ResourceManager* resources)
{
    const CL1XmlNode* n = cl1_require(resources, path);
    if (!n->has("value")) throw CL_Error("Resource '" + path + "' has no value");
    return n->get("value");
}

// ===========================================================================
// Image loading
// ===========================================================================

CL_PixelBuffer::CL_PixelBuffer(int width, int height)
    : bmp(new CL1Bitmap)
{
    bmp->w = width;
    bmp->h = height;
    bmp->rgba.assign((size_t)width * height * 4, 0);
}

CL_Color CL_PixelBuffer::get_pixel(int x, int y) const
{
    if (!bmp || x < 0 || y < 0 || x >= bmp->w || y >= bmp->h)
        return CL_Color(0, 0, 0, 0);
    const unsigned char* p = &bmp->rgba[((size_t)y * bmp->w + x) * 4];
    return CL_Color(p[0], p[1], p[2], p[3]);
}

CL_PixelBuffer CL_PixelBuffer::copy() const
{
    CL_PixelBuffer c;
    if (bmp) c.bmp.reset(new CL1Bitmap(*bmp));
    return c;
}

namespace {

unsigned int readLE16(const unsigned char* p) { return p[0] | (p[1] << 8); }

// Loads a Targa file (uncompressed or RLE; true color, grey or color mapped).
bool loadTga(const std::string& filename, CL1Bitmap& out, std::string& error)
{
    std::ifstream in(filename.c_str(), std::ios::binary);
    if (!in)
    {
        error = "IODeviceProvider_File::IODeviceProvider_File(): Unable to open file '" + filename + "'";
        return false;
    }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 18) { error = "Invalid TGA file '" + filename + "'"; return false; }

    const int idLen = d[0];
    const int cmType = d[1];
    const int imType = d[2];
    const unsigned int cmStart = readLE16(&d[3]);
    const unsigned int cmLen = readLE16(&d[5]);
    const int cmBits = d[7];
    const int w = (int)readLE16(&d[12]);
    const int h = (int)readLE16(&d[14]);
    const int bpp = d[16];
    const int desc = d[17];

    bool rle = (imType == 9 || imType == 10 || imType == 11);
    bool mapped = (imType == 1 || imType == 9);
    bool grey = (imType == 3 || imType == 11);
    bool truecolor = (imType == 2 || imType == 10);
    if (!(mapped || grey || truecolor) || w <= 0 || h <= 0)
    {
        error = "Unsupported TGA format in '" + filename + "'";
        return false;
    }
    if (!((bpp == 8) || (bpp == 15) || (bpp == 16) || (bpp == 24) || (bpp == 32)))
    {
        error = "Unsupported TGA pixel depth in '" + filename + "'";
        return false;
    }

    size_t pos = 18 + (size_t)idLen;

    // Palette
    std::vector<unsigned char> palette;   // RGBA entries
    if (cmType == 1)
    {
        int entryBytes = (cmBits + 7) / 8;
        if (pos + (size_t)cmLen * entryBytes > d.size()) { error = "Truncated TGA file '" + filename + "'"; return false; }
        palette.assign((size_t)(cmStart + cmLen) * 4, 0);
        for (unsigned int i = 0; i < cmLen; ++i)
        {
            const unsigned char* e = &d[pos + (size_t)i * entryBytes];
            unsigned char* o = &palette[(size_t)(cmStart + i) * 4];
            if (entryBytes >= 3) { o[0] = e[2]; o[1] = e[1]; o[2] = e[0]; o[3] = (entryBytes == 4) ? e[3] : 255; }
            else if (entryBytes == 2)
            {
                unsigned int v = readLE16(e);
                o[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31);
                o[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
                o[2] = (unsigned char)((v & 31) * 255 / 31);
                o[3] = 255;
            }
        }
        pos += (size_t)cmLen * entryBytes;
    }
    else if (mapped)
    {
        error = "TGA file '" + filename + "' needs a color map";
        return false;
    }

    const int bytesPP = (bpp + 7) / 8;
    const size_t numPixels = (size_t)w * h;

    // Raw pixel data (decoded RLE)
    std::vector<unsigned char> raw;
    raw.reserve(numPixels * bytesPP);
    if (!rle)
    {
        size_t need = numPixels * bytesPP;
        if (pos + need > d.size()) { error = "Truncated TGA file '" + filename + "'"; return false; }
        raw.assign(d.begin() + pos, d.begin() + pos + need);
    }
    else
    {
        size_t count = 0;
        while (count < numPixels && pos < d.size())
        {
            unsigned char hdr = d[pos++];
            size_t run = (size_t)(hdr & 0x7f) + 1;
            if (hdr & 0x80)
            {
                if (pos + bytesPP > d.size()) break;
                for (size_t i = 0; i < run && count < numPixels; ++i, ++count)
                    raw.insert(raw.end(), d.begin() + pos, d.begin() + pos + bytesPP);
                pos += bytesPP;
            }
            else
            {
                size_t bytes = run * bytesPP;
                if (pos + bytes > d.size()) break;
                size_t take = std::min(run, numPixels - count);
                raw.insert(raw.end(), d.begin() + pos, d.begin() + pos + take * bytesPP);
                pos += bytes;
                count += take;
            }
        }
        if (count < numPixels) { error = "Truncated TGA file '" + filename + "'"; return false; }
    }

    out.w = w;
    out.h = h;
    out.rgba.assign(numPixels * 4, 255);

    const bool topDown = (desc & 0x20) != 0;
    const bool rightToLeft = (desc & 0x10) != 0;
    const int alphaBits = desc & 0x0f;

    for (int y = 0; y < h; ++y)
    {
        int dy = topDown ? y : (h - 1 - y);
        for (int x = 0; x < w; ++x)
        {
            int dx = rightToLeft ? (w - 1 - x) : x;
            const unsigned char* s = &raw[((size_t)y * w + x) * bytesPP];
            unsigned char* o = &out.rgba[((size_t)dy * w + dx) * 4];
            if (mapped)
            {
                unsigned int idx = s[0];
                if (idx * 4 + 3 < palette.size()) std::memcpy(o, &palette[idx * 4], 4);
            }
            else if (grey)
            {
                o[0] = o[1] = o[2] = s[0];
                o[3] = (bpp == 16) ? s[1] : 255;
            }
            else if (bytesPP == 4)
            {
                o[0] = s[2]; o[1] = s[1]; o[2] = s[0];
                o[3] = (alphaBits == 0) ? 255 : s[3];
            }
            else if (bytesPP == 3)
            {
                o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255;
            }
            else // 15/16 bit: A1R5G5B5
            {
                unsigned int v = readLE16(s);
                o[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31);
                o[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
                o[2] = (unsigned char)((v & 31) * 255 / 31);
                o[3] = (bpp == 16 && alphaBits > 0) ? ((v & 0x8000) ? 255 : 0) : 255;
            }
        }
    }
    return true;
}

// Fallback for non-TGA files: let ClanLib decode them.
bool loadWithClanLib(const std::string& filename, CL1Bitmap& out, std::string& error)
{
    try
    {
        clan::PixelBuffer pb(filename);
        pb = pb.to_format(clan::TextureFormat::rgba8);
        out.w = pb.get_width();
        out.h = pb.get_height();
        out.rgba.resize((size_t)out.w * out.h * 4);
        for (int y = 0; y < out.h; ++y)
            std::memcpy(&out.rgba[(size_t)y * out.w * 4], pb.get_line_uint8(y), (size_t)out.w * 4);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("Unable to load image '") + filename + "': " + e.what();
        return false;
    }
}

CL_PixelBuffer loadImageFile(const std::string& filename)
{
    CL_PixelBuffer pb;
    pb.bmp.reset(new CL1Bitmap);
    std::string error;
    bool ok = (cl1_extension(filename) == "tga")
                  ? loadTga(filename, *pb.bmp, error)
                  : loadWithClanLib(filename, *pb.bmp, error);
    if (!ok) throw CL_Error(error);
    return pb;
}

} // namespace

CL_PixelBuffer CL_ProviderFactory::load(const std::string& filename)
{
    return loadImageFile(filename);
}

CL_TargaProvider::CL_TargaProvider(const std::string& filename)
    : CL_PixelBuffer(loadImageFile(filename))
{
}

// ===========================================================================
// Window, canvas, input
// ===========================================================================

namespace {

// These objects are deliberately never destroyed by static destruction: at
// process exit ClanLib's graphics teardown throws, which aborts the program.
// Explicit destruction (CL_DisplayWindow destructor) still works.
clan::DisplayWindow* g_window = 0;

void destroyWindow()
{
    clan::DisplayWindow* w = g_window;
    g_window = 0;
    delete w;
}
// Created on first use (avoids static initialization order problems)
clan::Canvas& canvasObject() { static clan::Canvas* c = new clan::Canvas; return *c; }
clan::Slot& slotKeyDown() { static clan::Slot* s = new clan::Slot; return *s; }
clan::Slot& slotClose() { static clan::Slot* s = new clan::Slot; return *s; }
clan::Slot& slotKeyUp() { static clan::Slot* s = new clan::Slot; return *s; }
std::map<int, std::string> g_keyText;
CL1KeySignal g_keyUp;

clan::Canvas& canvas()
{
    if (!g_window) throw CL_Error("No display window has been created");
    return canvasObject();
}

clan::Colorf premultiplied(const CL_Color& c)
{
    float a = c.get_alpha() / 255.0f;
    return clan::Colorf(c.get_red() / 255.0f * a, c.get_green() / 255.0f * a,
                        c.get_blue() / 255.0f * a, a);
}

clan::Rectf toRectf(const CL_Rect& r)
{
    return clan::Rectf((float)r.left, (float)r.top, (float)r.right, (float)r.bottom);
}

void closeRequested()
{
    std::exit(0);
}

} // namespace

CL_DisplayWindow::CL_DisplayWindow(const std::string& title, int width, int height, bool fullscreen)
{
    clan::OpenGLTarget::set_current();

    slotKeyDown() = clan::Slot();
    slotKeyUp() = clan::Slot();
    slotClose() = clan::Slot();
    canvasObject() = clan::Canvas();
    destroyWindow();

    g_window = new clan::DisplayWindow(title, (float)width, (float)height, fullscreen);
    canvasObject() = clan::Canvas(*g_window);

    // All our images are premultiplied, so use premultiplied blending.
    canvasObject().set_blend_state(clan::BlendState(canvasObject().get_gc(), clan::BlendStateDescription::blend(true)));

    slotKeyDown() = g_window->get_keyboard().sig_key_down().connect(
        std::function<void(const clan::InputEvent&)>([](const clan::InputEvent& e)
        {
            g_keyText[(int)e.id] = e.str;
        }));

    slotKeyUp() = g_window->get_keyboard().sig_key_up().connect(
        std::function<void(const clan::InputEvent&)>([](const clan::InputEvent& e)
        {
            clan::InputEvent ev = e;
            if (ev.str.empty())
            {
                // ClanLib 1.0 delivered the typed character with the key release.
                std::map<int, std::string>::iterator it = g_keyText.find((int)e.id);
                if (it != g_keyText.end()) ev.str = it->second;
            }
            g_keyText.erase((int)e.id);
            g_keyUp.fire(ev);
        }));

    slotClose() = g_window->sig_window_close().connect(std::function<void()>(closeRequested));
}

CL_DisplayWindow::~CL_DisplayWindow()
{
    slotKeyDown() = clan::Slot();
    slotKeyUp() = clan::Slot();
    slotClose() = clan::Slot();
    canvasObject() = clan::Canvas();
    destroyWindow();
}

void CL_DisplayWindow::set_fullscreen(int, int)
{
    if (g_window && !g_window->is_fullscreen()) g_window->toggle_fullscreen();
}

void CL_DisplayWindow::set_windowed()
{
    if (g_window && g_window->is_fullscreen()) g_window->toggle_fullscreen();
}

void CL_DisplayWindow::set_size(int width, int height)
{
    if (g_window) g_window->set_size((float)width, (float)height, true);
}

void CL_Display::flip()
{
    clan::Canvas& c = canvas();
    c.flush();
    g_window->flip(1);
}

void CL_Display::clear()
{
    canvas().clear(clan::Colorf(0.0f, 0.0f, 0.0f, 1.0f));
}

void CL_Display::fill_rect(const CL_Rect& rect, const CL_Color& color)
{
    canvas().fill_rect(toRectf(rect), premultiplied(color));
}

void CL_Display::draw_line(int x1, int y1, int x2, int y2, const CL_Color& color)
{
    canvas().draw_line((float)x1, (float)y1, (float)x2, (float)y2, premultiplied(color));
}

void CL_Display::set_cliprect(const CL_Rect& rect)
{
    canvas().set_cliprect(toRectf(rect));
}

// --- key signal -------------------------------------------------------------

CL_Slot CL1KeySignal::add(Handler handler)
{
    CL_Slot slot;
    slot.id = nextId++;
    handlers.push_back(std::make_pair(slot.id, handler));
    return slot;
}

void CL1KeySignal::disconnect(const CL_Slot& slot)
{
    for (size_t i = 0; i < handlers.size(); ++i)
    {
        if (handlers[i].first == slot.id)
        {
            handlers.erase(handlers.begin() + i);
            return;
        }
    }
}

void CL1KeySignal::fire(const CL_InputEvent& e)
{
    // Handlers may connect/disconnect while we dispatch: work on a copy and
    // check that a handler is still connected before calling it.
    std::vector<std::pair<int, Handler> > copy = handlers;
    for (size_t i = 0; i < copy.size(); ++i)
    {
        bool stillThere = false;
        for (size_t k = 0; k < handlers.size(); ++k)
            if (handlers[k].first == copy[i].first) { stillThere = true; break; }
        if (stillThere) copy[i].second(e);
    }
}

CL1KeySignal& CL_Keyboard::sig_key_up()
{
    return g_keyUp;
}

bool CL_Keyboard::get_keycode(int keycode)
{
    if (!g_window) return false;
    return g_window->get_keyboard().get_keycode(keycode);
}

int CL_Mouse::get_x()
{
    if (!g_window) return 0;
    return (int)g_window->get_mouse().get_position().x;
}

int CL_Mouse::get_y()
{
    if (!g_window) return 0;
    return (int)g_window->get_mouse().get_position().y;
}

bool CL_Mouse::get_keycode(int keycode)
{
    if (!g_window) return false;
    return g_window->get_mouse().get_keycode(keycode);
}

void CL_Mouse::show()
{
    if (g_window) g_window->show_cursor();
}

void CL_Mouse::hide()
{
    if (g_window) g_window->hide_cursor();
}

unsigned int CL_System::get_time()
{
    static const uint64_t base = clan::System::get_time();
    return (unsigned int)(clan::System::get_time() - base);
}

#ifdef __APPLE__
void cl1_cocoa_pump_events();   // cl1_cocoa.mm
#endif

void CL_System::keep_alive()
{
#ifdef __APPLE__
    cl1_cocoa_pump_events();
#endif
    if (!clan::RunLoop::process(0))
        std::exit(0);
}

bool CL_DirectoryScanner::scan(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool CL_Directory::create(const std::string& path)
{
    return mkdir(path.c_str(), 0755) == 0;
}

// ===========================================================================
// Textures, surfaces, sprites, fonts
// ===========================================================================

struct CL1Texture
{
    std::shared_ptr<CL1Bitmap> bmp;
    clan::Image image;
    bool uploaded;
    float tint[4];

    CL1Texture() : uploaded(false) { tint[0] = tint[1] = tint[2] = tint[3] = 1.0f; }

    void ensure()
    {
        if (uploaded) return;
        clan::Canvas& c = canvas();
        std::vector<unsigned char> pm(bmp->rgba);
        for (size_t i = 0; i + 3 < pm.size(); i += 4)
        {
            unsigned int a = pm[i + 3];
            pm[i]     = (unsigned char)((pm[i]     * a + 127) / 255);
            pm[i + 1] = (unsigned char)((pm[i + 1] * a + 127) / 255);
            pm[i + 2] = (unsigned char)((pm[i + 2] * a + 127) / 255);
        }
        clan::PixelBuffer pb(bmp->w, bmp->h, clan::TextureFormat::rgba8, pm.data());
        image = clan::Image(c, pb, clan::Rect(0, 0, bmp->w, bmp->h));
        uploaded = true;
    }

    void draw(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh,
              float r, float g, float b, float a)
    {
        if (!bmp || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
        ensure();
        if (tint[0] != r || tint[1] != g || tint[2] != b || tint[3] != a)
        {
            image.set_color(clan::Colorf(r, g, b, a));
            tint[0] = r; tint[1] = g; tint[2] = b; tint[3] = a;
        }
        image.draw(canvas(),
                   clan::Rectf((float)sx, (float)sy, (float)(sx + sw), (float)(sy + sh)),
                   clan::Rectf(dx, dy, dx + dw, dy + dh));
    }
};

namespace {

// Textures loaded from files are shared (gui/button is loaded several times...).
std::map<std::string, std::shared_ptr<CL1Texture> >& textureCache()
{
    static std::map<std::string, std::shared_ptr<CL1Texture> >* cache =
        new std::map<std::string, std::shared_ptr<CL1Texture> >;
    return *cache;
}

std::shared_ptr<CL1Texture> loadTexture(const std::string& filename)
{
    std::map<std::string, std::shared_ptr<CL1Texture> >::iterator it = textureCache().find(filename);
    if (it != textureCache().end()) return it->second;

    CL_PixelBuffer pb = loadImageFile(filename);
    std::shared_ptr<CL1Texture> t(new CL1Texture);
    t->bmp = pb.bmp;
    textureCache()[filename] = t;
    return t;
}

struct FrameRect { int x, y, w, h; };

// Cuts frames out of an image using its transparency. First finds a band of
// lines (up to the next completely transparent line), then the frames within
// the band (separated by completely transparent columns).
std::vector<FrameRect> alphaClip(const CL1Bitmap& b)
{
    std::vector<FrameRect> frames;
    const int limit = 13;     // ~5% alpha
    std::vector<char> rowUsed(b.h, 0);
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            if (b.rgba[((size_t)y * b.w + x) * 4 + 3] > limit) { rowUsed[y] = 1; break; }

    int y = 0;
    while (y < b.h)
    {
        while (y < b.h && !rowUsed[y]) ++y;
        if (y >= b.h) break;
        int y0 = y;
        while (y < b.h && rowUsed[y]) ++y;
        int y1 = y;                          // exclusive

        int x = 0;
        while (x < b.w)
        {
            bool colUsed = false;
            for (int yy = y0; yy < y1 && !colUsed; ++yy)
                colUsed = b.rgba[((size_t)yy * b.w + x) * 4 + 3] > limit;
            if (!colUsed) { ++x; continue; }
            int x0 = x;
            for (;;)
            {
                ++x;
                if (x >= b.w) break;
                bool used = false;
                for (int yy = y0; yy < y1 && !used; ++yy)
                    used = b.rgba[((size_t)yy * b.w + x) * 4 + 3] > limit;
                if (!used) break;
            }
            FrameRect f = { x0, y0, x - x0, y1 - y0 };
            frames.push_back(f);
        }
    }
    return frames;
}

bool parsePair(const std::string& s, int& a, int& b)
{
    return std::sscanf(s.c_str(), " %d , %d", &a, &b) == 2;
}

} // namespace

// --- CL_Surface -------------------------------------------------------------

CL_Surface::CL_Surface() : sx(0), sy(0), sw(0), sh(0), cr(1), cg(1), cb(1), ca(1) {}

CL_Surface::CL_Surface(const CL_PixelBuffer& pixels)
    : sx(0), sy(0), sw(pixels.get_width()), sh(pixels.get_height()), cr(1), cg(1), cb(1), ca(1)
{
    if (pixels.bmp)
    {
        tex.reset(new CL1Texture);
        tex->bmp = pixels.bmp;
    }
}

CL_Surface::CL_Surface(const std::string& resourceName, CL_ResourceManager* resources)
    : sx(0), sy(0), sw(0), sh(0), cr(1), cg(1), cb(1), ca(1)
{
    const CL1XmlNode* n = cl1_require(resources, resourceName);
    std::string file = n->get("file");
    if (file.empty())
    {
        const CL1XmlNode* img = n->child("image");
        if (img) file = img->get("file");
    }
    if (file.empty()) throw CL_Error("Resource '" + resourceName + "' is not a surface");

    tex = loadTexture(cl1_resource_file(resources, file));
    int iw = tex->bmp->w, ih = tex->bmp->h;
    sx = std::atoi(n->get("x", "0").c_str());
    sy = std::atoi(n->get("y", "0").c_str());
    sw = std::atoi(n->get("width", "0").c_str());
    sh = std::atoi(n->get("height", "0").c_str());
    if (sx < 0 || sx >= iw) sx = 0;
    if (sy < 0 || sy >= ih) sy = 0;
    if (sw <= 0 || sx + sw > iw) sw = iw - sx;
    if (sh <= 0 || sy + sh > ih) sh = ih - sy;
}

CL_Surface::CL_Surface(const CL_Surface& other)
    : tex(other.tex), sx(other.sx), sy(other.sy), sw(other.sw), sh(other.sh),
      cr(other.cr), cg(other.cg), cb(other.cb), ca(other.ca)
{
}

CL_Surface& CL_Surface::operator=(const CL_Surface& other)
{
    tex = other.tex; sx = other.sx; sy = other.sy; sw = other.sw; sh = other.sh;
    cr = other.cr; cg = other.cg; cb = other.cb; ca = other.ca;
    return *this;
}

CL_Surface::~CL_Surface() {}

int CL_Surface::get_width() const  { return sw; }
int CL_Surface::get_height() const { return sh; }

void CL_Surface::drawRegion(int srcX, int srcY, int srcW, int srcH,
                            float dx, float dy, float dw, float dh) const
{
    if (!tex) return;
    tex->draw(sx + srcX, sy + srcY, srcW, srcH, dx, dy, dw, dh, cr, cg, cb, ca);
}

void CL_Surface::draw(float x, float y) const
{
    drawRegion(0, 0, sw, sh, std::floor(x + 0.5f), std::floor(y + 0.5f), (float)sw, (float)sh);
}

void CL_Surface::draw(const CL_Rect& dest) const
{
    drawRegion(0, 0, sw, sh, (float)dest.left, (float)dest.top,
               (float)(dest.right - dest.left), (float)(dest.bottom - dest.top));
}

void CL_Surface::draw(const CL_Rect& src, const CL_Rect& dest) const
{
    drawRegion(src.left, src.top, src.right - src.left, src.bottom - src.top,
               (float)dest.left, (float)dest.top,
               (float)(dest.right - dest.left), (float)(dest.bottom - dest.top));
}

void CL_Surface::set_color(float r, float g, float b, float a)
{
    cr = r; cg = g; cb = b; ca = a;
}

CL_PixelBuffer CL_Surface::get_pixeldata() const
{
    CL_PixelBuffer out(sw, sh);
    if (!tex || !tex->bmp) return out;
    const CL1Bitmap& b = *tex->bmp;
    for (int y = 0; y < sh; ++y)
        std::memcpy(&out.bmp->rgba[(size_t)y * sw * 4],
                    &b.rgba[((size_t)(sy + y) * b.w + sx) * 4], (size_t)sw * 4);
    return out;
}

void CL_Surface::rotate(float degrees)
{
    if (!tex || !tex->bmp) return;

    CL_PixelBuffer src = get_pixeldata();
    std::shared_ptr<CL1Bitmap> dst(new CL1Bitmap);
    dst->w = sw;
    dst->h = sh;
    dst->rgba.assign((size_t)sw * sh * 4, 0);

    const double rad = (double)CL1_ROTATE_SIGN * degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);
    const double cx = sw / 2.0, cy = sh / 2.0;
    const unsigned char* sp = src.bmp->rgba.data();

    for (int y = 0; y < sh; ++y)
    {
        for (int x = 0; x < sw; ++x)
        {
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            // inverse of a clockwise rotation (y axis points down)
            double fx = dx * c + dy * s + cx - 0.5;
            double fy = -dx * s + dy * c + cy - 0.5;
            int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            double tx = fx - x0, ty = fy - y0;

            double acc[4] = { 0, 0, 0, 0 };      // premultiplied r,g,b and alpha
            for (int j = 0; j < 2; ++j)
            {
                for (int i = 0; i < 2; ++i)
                {
                    int px = x0 + i, py = y0 + j;
                    double wgt = (i ? tx : 1.0 - tx) * (j ? ty : 1.0 - ty);
                    if (px < 0 || py < 0 || px >= sw || py >= sh || wgt <= 0.0) continue;
                    const unsigned char* p = sp + ((size_t)py * sw + px) * 4;
                    double a = p[3] / 255.0;
                    acc[0] += wgt * p[0] * a;
                    acc[1] += wgt * p[1] * a;
                    acc[2] += wgt * p[2] * a;
                    acc[3] += wgt * a;
                }
            }
            unsigned char* o = &dst->rgba[((size_t)y * sw + x) * 4];
            if (acc[3] > 0.0)
            {
                o[0] = (unsigned char)std::min(255.0, acc[0] / acc[3] + 0.5);
                o[1] = (unsigned char)std::min(255.0, acc[1] / acc[3] + 0.5);
                o[2] = (unsigned char)std::min(255.0, acc[2] / acc[3] + 0.5);
                o[3] = (unsigned char)std::min(255.0, acc[3] * 255.0 + 0.5);
            }
        }
    }

    std::shared_ptr<CL1Texture> nt(new CL1Texture);
    nt->bmp = dst;
    tex = nt;
    sx = sy = 0;
}

// --- CL_Sprite --------------------------------------------------------------

CL_Sprite::CL_Sprite(const std::string& resourceName, CL_ResourceManager* resources)
    : current(0), delay(60), loop(true), timeAcc(0.0f), lastUpdate(CL_System::get_time())
{
    const CL1XmlNode* n = cl1_require(resources, resourceName);
    const CL1XmlNode* img = n->child("image");
    if (!img) throw CL_Error("Resource '" + resourceName + "' is not a sprite");

    tex = loadTexture(cl1_resource_file(resources, img->get("file")));
    const CL1Bitmap& b = *tex->bmp;

    const CL1XmlNode* grid = img->child("grid");
    if (grid)
    {
        int px = 0, py = 0, fw = b.w, fh = b.h, cols = 1, rows = 1;
        parsePair(grid->get("pos"), px, py);
        parsePair(grid->get("size"), fw, fh);
        parsePair(grid->get("array"), cols, rows);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                Frame f = { px + c * fw, py + r * fh, fw, fh };
                frames.push_back(f);
            }
    }
    else if (img->child("alpha"))
    {
        std::vector<FrameRect> clipped = alphaClip(b);
        for (size_t i = 0; i < clipped.size(); ++i)
        {
            Frame f = { clipped[i].x, clipped[i].y, clipped[i].w, clipped[i].h };
            frames.push_back(f);
        }
    }
    if (frames.empty())
    {
        Frame f = { 0, 0, b.w, b.h };
        frames.push_back(f);
    }

    const CL1XmlNode* anim = n->child("animation");
    if (anim)
    {
        if (anim->has("speed")) delay = std::max(1, std::atoi(anim->get("speed").c_str()));
        std::string l = cl1_lower(anim->get("loop", "yes"));
        loop = !(l == "no" || l == "false" || l == "0");
    }
}

CL_Sprite::~CL_Sprite() {}

int CL_Sprite::get_width() const  { return frames[current].w; }
int CL_Sprite::get_height() const { return frames[current].h; }

void CL_Sprite::set_frame(int frame)
{
    if (frame < 0) frame = 0;
    if (frame >= (int)frames.size()) frame = (int)frames.size() - 1;
    current = frame;
}

void CL_Sprite::update()
{
    unsigned int now = CL_System::get_time();
    unsigned int elapsed = now - lastUpdate;
    lastUpdate = now;
    if (elapsed > 1000) elapsed = 1000;

    timeAcc += (float)elapsed;
    while (timeAcc >= (float)delay)
    {
        timeAcc -= (float)delay;
        if (current + 1 < (int)frames.size()) ++current;
        else if (loop) current = 0;
    }
}

void CL_Sprite::draw(float x, float y) const
{
    const Frame& f = frames[current];
    tex->draw(f.x, f.y, f.w, f.h, std::floor(x + 0.5f), std::floor(y + 0.5f),
              (float)f.w, (float)f.h, 1.0f, 1.0f, 1.0f, 1.0f);
}

// --- CL_Font ----------------------------------------------------------------

CL_Font::CL_Font(const std::string& resourceName, CL_ResourceManager* resources)
    : glyphTop(0), glyphHeight(0), spaceLen(8),
      alignment(clan::Origin::top_left), alignDx(0), alignDy(0)
{
    for (int i = 0; i < 256; ++i) { glyphs[i].x = 0; glyphs[i].w = 0; glyphs[i].valid = false; }

    const CL1XmlNode* n = cl1_require(resources, resourceName);
    const CL1XmlNode* bitmap = n->child("bitmap");
    if (!bitmap) throw CL_Error("Resource '" + resourceName + "' is not a bitmap font");

    const CL1XmlNode* spriteNode = cl1_require(resources, bitmap->get("glyphs"));
    const CL1XmlNode* img = spriteNode->child("image");
    if (!img) throw CL_Error("Font glyphs '" + bitmap->get("glyphs") + "' have no image");

    tex = loadTexture(cl1_resource_file(resources, img->get("file")));
    spaceLen = std::atoi(bitmap->get("spacelen", "8").c_str());

    std::vector<FrameRect> frames = alphaClip(*tex->bmp);
    const std::string letters = bitmap->get("letters");

    // Some glyphs consist of two separate parts (e.g. the double quote). If
    // we found more pieces than letters, join the closest neighbours.
    while (frames.size() > letters.size() && frames.size() > 1)
    {
        size_t best = 0;
        int bestGap = 1 << 30;
        for (size_t i = 0; i + 1 < frames.size(); ++i)
        {
            if (frames[i].y != frames[i + 1].y) continue;       // different line
            int gap = frames[i + 1].x - (frames[i].x + frames[i].w);
            if (gap < bestGap) { bestGap = gap; best = i; }
        }
        if (bestGap == (1 << 30)) break;
        frames[best].w = frames[best + 1].x + frames[best + 1].w - frames[best].x;
        frames.erase(frames.begin() + best + 1);
    }
    if (frames.size() != letters.size())
        std::cerr << "Warning: font '" << resourceName << "' has " << frames.size()
                  << " glyphs but " << letters.size() << " letters" << std::endl;

    int top = 1 << 30, bottom = 0;
    size_t count = std::min(frames.size(), letters.size());
    for (size_t i = 0; i < count; ++i)
    {
        unsigned char c = (unsigned char)letters[i];
        glyphs[c].x = frames[i].x;
        glyphs[c].w = frames[i].w;
        glyphs[c].valid = true;
        top = std::min(top, frames[i].y);
        bottom = std::max(bottom, frames[i].y + frames[i].h);
    }
    if (count == 0) { top = 0; bottom = tex->bmp->h; }
    glyphTop = top;
    glyphHeight = bottom - top;
}

CL_Font::~CL_Font() {}

void CL_Font::set_alignment(clan::Origin origin, int dx, int dy)
{
    alignment = origin;
    alignDx = dx;
    alignDy = dy;
}

int CL_Font::get_height() const { return glyphHeight; }

int CL_Font::get_width(const std::string& text) const
{
    int w = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = (unsigned char)text[i];
        if (c == ' ') w += spaceLen;
        else if (glyphs[c].valid) w += glyphs[c].w;
    }
    return w;
}

void CL_Font::draw(float x, float y, const std::string& text) const
{
    const int width = get_width(text);
    float px = x + alignDx;
    float py = y + alignDy;

    switch (alignment)
    {
    case clan::Origin::top_center: case clan::Origin::center: case clan::Origin::bottom_center:
        px -= width / 2.0f; break;
    case clan::Origin::top_right: case clan::Origin::center_right: case clan::Origin::bottom_right:
        px -= (float)width; break;
    default: break;
    }
    switch (alignment)
    {
    case clan::Origin::center_left: case clan::Origin::center: case clan::Origin::center_right:
        py -= glyphHeight / 2.0f; break;
    case clan::Origin::bottom_left: case clan::Origin::bottom_center: case clan::Origin::bottom_right:
        py -= (float)glyphHeight; break;
    default: break;
    }

    px = std::floor(px + 0.5f);
    py = std::floor(py + 0.5f);

    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = (unsigned char)text[i];
        if (c == ' ') { px += spaceLen; continue; }
        if (!glyphs[c].valid) continue;
        tex->draw(glyphs[c].x, glyphTop, glyphs[c].w, glyphHeight, px, py,
                  (float)glyphs[c].w, (float)glyphHeight, 1.0f, 1.0f, 1.0f, 1.0f);
        px += glyphs[c].w;
    }
}

// ===========================================================================
// Sound
// ===========================================================================

static std::string soundFile(const std::string& resourceName, CL_ResourceManager* resources)
{
    const CL1XmlNode* n = cl1_require(resources, resourceName);
    if (!n->has("file")) throw CL_Error("Resource '" + resourceName + "' is not a sample");
    return cl1_resource_file(resources, n->get("file"));
}

CL_SoundBuffer::CL_SoundBuffer(const std::string& resourceName, CL_ResourceManager* resources)
    : clan::SoundBuffer(soundFile(resourceName, resources), false,
                        cl1_extension(soundFile(resourceName, resources)))
{
}

// ===========================================================================
// Application entry point
// ===========================================================================

static CL_ClanApplication* g_application = 0;

CL_ClanApplication::CL_ClanApplication()
{
    g_application = this;
}

namespace {

// The old ClanLib called main() of the global CL_ClanApplication object. With
// ClanLib 4 we get constructed by clanApp and run the whole game from here
// (the game has its own loops and calls CL_System::keep_alive()).
class CL1Runner : public clan::Application
{
public:
    CL1Runner() : started(false) {}

    // The game is NOT started from the constructor: on macOS the constructor
    // runs while the application is still launching, and blocking there means
    // the window never receives events. The first update() call comes from
    // the normal run loop, where the game may run its own loops.
    bool update() override
    {
        if (started) return false;
        started = true;

        std::vector<std::string> args = clan::Application::main_args();
        if (args.empty()) args.push_back("trophy");
        std::vector<char*> argv;
        for (size_t i = 0; i < args.size(); ++i) argv.push_back(const_cast<char*>(args[i].c_str()));
        argv.push_back(0);

        try
        {
            if (g_application) g_application->main((int)args.size(), &argv[0]);
            else std::cerr << "No application object found" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "An error occured: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "An unknown error occured" << std::endl;
        }
        return false;       // game is over: quit
    }

private:
    bool started;
};

#ifndef CL1_NO_RUNNER
clan::ApplicationInstance<CL1Runner> g_runnerInstance;
#endif

} // namespace
