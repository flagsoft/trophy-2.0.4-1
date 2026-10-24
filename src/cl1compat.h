/*
 * cl1compat.h - ClanLib 1.0 compatibility layer on top of ClanLib 4.x
 *
 * Trophy 2.0.4 was written against ClanLib 0.8/1.0. This header provides the
 * (small) subset of the old API that Trophy uses, implemented on top of
 * ClanLib 4.1, so that the game code needs almost no changes.
 *
 * Only what Trophy needs is implemented. This is not a general replacement
 * for ClanLib 1.0.
 */
#ifndef CL1COMPAT_H
#define CL1COMPAT_H

#include <ClanLib/core.h>
#include <ClanLib/display.h>
#include <ClanLib/GL/opengl_target.h>
#include <ClanLib/sound.h>
#include <ClanLib/application.h>

// The old ClanLib headers pulled these in; Apple's libc++ is stricter than
// other standard libraries, so include what the game code may rely on.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <list>
#include <numeric>
#include <set>
#include <unistd.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Simple type aliases
// ---------------------------------------------------------------------------

typedef clan::Exception              CL_Error;        // has public 'message'
typedef clan::Color                  CL_Color;
typedef clan::Rect                   CL_Rect;
typedef clan::InputEvent             CL_InputEvent;
typedef clan::SoundOutput            CL_SoundOutput;
typedef clan::SoundBuffer_Session    CL_SoundBuffer_Session;

// The old CL_Setup* objects only had to exist. clanApp does the setup now.
struct CL_SetupCore    {};
struct CL_SetupDisplay {};
struct CL_SetupGL      {};
struct CL_SetupSDL     {};
struct CL_SetupSound   {};

// ---------------------------------------------------------------------------
// Origins (used for font alignment)
// ---------------------------------------------------------------------------

static const clan::Origin origin_top_left      = clan::Origin::top_left;
static const clan::Origin origin_top_center    = clan::Origin::top_center;
static const clan::Origin origin_top_right     = clan::Origin::top_right;
static const clan::Origin origin_center_left   = clan::Origin::center_left;
static const clan::Origin origin_center        = clan::Origin::center;
static const clan::Origin origin_center_right  = clan::Origin::center_right;
static const clan::Origin origin_bottom_left   = clan::Origin::bottom_left;
static const clan::Origin origin_bottom_center = clan::Origin::bottom_center;
static const clan::Origin origin_bottom_right  = clan::Origin::bottom_right;

// ---------------------------------------------------------------------------
// Key codes. ClanLib 4 uses platform specific InputCode values, so we map the
// old CL_KEY_* names to them.
// ---------------------------------------------------------------------------

enum CL1_KeyCodes
{
    CL_KEY_ESCAPE    = clan::keycode_escape,
    CL_KEY_ENTER     = clan::keycode_return,
    CL_KEY_SPACE     = clan::keycode_space,
    CL_KEY_BACKSPACE = clan::keycode_backspace,
    CL_KEY_DELETE    = clan::keycode_delete,
    CL_KEY_HOME      = clan::keycode_home,
    CL_KEY_END       = clan::keycode_end,
    CL_KEY_PRIOR     = clan::keycode_prior,
    CL_KEY_NEXT      = clan::keycode_next,
    CL_KEY_LEFT      = clan::keycode_left,
    CL_KEY_UP        = clan::keycode_up,
    CL_KEY_RIGHT     = clan::keycode_right,
    CL_KEY_DOWN      = clan::keycode_down,
    CL_KEY_A = clan::keycode_a, CL_KEY_B = clan::keycode_b, CL_KEY_C = clan::keycode_c,
    CL_KEY_D = clan::keycode_d, CL_KEY_E = clan::keycode_e, CL_KEY_F = clan::keycode_f,
    CL_KEY_G = clan::keycode_g, CL_KEY_H = clan::keycode_h, CL_KEY_I = clan::keycode_i,
    CL_KEY_J = clan::keycode_j, CL_KEY_K = clan::keycode_k, CL_KEY_L = clan::keycode_l,
    CL_KEY_M = clan::keycode_m, CL_KEY_N = clan::keycode_n, CL_KEY_O = clan::keycode_o,
    CL_KEY_P = clan::keycode_p, CL_KEY_Q = clan::keycode_q, CL_KEY_R = clan::keycode_r,
    CL_KEY_S = clan::keycode_s, CL_KEY_T = clan::keycode_t, CL_KEY_U = clan::keycode_u,
    CL_KEY_V = clan::keycode_v, CL_KEY_W = clan::keycode_w, CL_KEY_X = clan::keycode_x,
    CL_KEY_Y = clan::keycode_y, CL_KEY_Z = clan::keycode_z,
    CL_MOUSE_LEFT    = clan::mouse_left
};

// ---------------------------------------------------------------------------
// Pixel data (CPU side, always straight-alpha RGBA, 8 bit per channel)
// ---------------------------------------------------------------------------

struct CL1Bitmap
{
    int w = 0;
    int h = 0;
    std::vector<unsigned char> rgba;   // w*h*4, order R,G,B,A, top row first
};

class CL_PixelBuffer
{
public:
    CL_PixelBuffer() {}
    CL_PixelBuffer(int width, int height);

    bool is_null() const { return !bmp; }
    int  get_width()  const { return bmp ? bmp->w : 0; }
    int  get_height() const { return bmp ? bmp->h : 0; }

    // Kept for source compatibility: pixel data needs no locking here.
    void lock() {}
    void unlock() {}

    //! Returns the pixel at x,y (transparent black when outside).
    CL_Color get_pixel(int x, int y) const;

    //! Raw RGBA bytes (R,G,B,A per pixel).
    unsigned char* get_data() { return bmp ? bmp->rgba.data() : nullptr; }
    const unsigned char* get_data() const { return bmp ? bmp->rgba.data() : nullptr; }

    //! Deep copy
    CL_PixelBuffer copy() const;

    std::shared_ptr<CL1Bitmap> bmp;
};

//! Loads an image file (TGA natively, other formats through ClanLib).
class CL_ProviderFactory
{
public:
    static CL_PixelBuffer load(const std::string& filename);
};

//! In ClanLib 1.0 this was an image provider. Here it is simply a loaded buffer.
class CL_TargaProvider : public CL_PixelBuffer
{
public:
    explicit CL_TargaProvider(const std::string& filename);
};

// ---------------------------------------------------------------------------
// Resources (resources.xml)
// ---------------------------------------------------------------------------

struct CL1XmlNode;

class CL_ResourceManager
{
public:
    //! Throws CL_Error if the file cannot be read.
    explicit CL_ResourceManager(const std::string& xmlFile);
    ~CL_ResourceManager();

    //! Directory of the xml file (with trailing slash). File names in the
    //! resources are relative to it.
    const std::string& base_dir() const { return baseDir; }

    //! Finds an element by path, e.g. "cars/vw/length". Returns 0 if not found.
    const CL1XmlNode* find(const std::string& path) const;

private:
    std::string baseDir;
    std::unique_ptr<CL1XmlNode> root;
};

//! Integer resource: int v = CL_Integer("cars/vw/length", resources);
class CL_Integer
{
public:
    CL_Integer(const std::string& path, CL_ResourceManager* resources);
    operator int() const { return value; }
private:
    int value;
};

class CL_String
{
public:
    static std::string load(const std::string& path, CL_ResourceManager* resources);
};

// ---------------------------------------------------------------------------
// Surfaces, sprites, fonts
// ---------------------------------------------------------------------------

struct CL1Texture;

class CL_Surface
{
public:
    CL_Surface();
    CL_Surface(const CL_PixelBuffer& pixels);
    CL_Surface(const std::string& resourceName, CL_ResourceManager* resources);
    CL_Surface(const CL_Surface& other);
    CL_Surface& operator=(const CL_Surface& other);
    ~CL_Surface();

    int get_width() const;
    int get_height() const;

    void draw(float x, float y) const;
    void draw(const CL_Rect& dest) const;
    void draw(const CL_Rect& src, const CL_Rect& dest) const;
    void put_screen(float x, float y) const { draw(x, y); }

    //! Rotates the pixel data clockwise by 'degrees' around the center. The
    //! size stays the same. Only this surface is changed (not its copies).
    void rotate(float degrees);

    //! Multiplies the surface color with (r,g,b,a) when drawing. 0.0 - 1.0
    void set_color(float r, float g, float b, float a = 1.0f);

    //! Copy of the (unrotated, untinted) pixel data of the visible part.
    CL_PixelBuffer get_pixeldata() const;

private:
    std::shared_ptr<CL1Texture> tex;
    int sx, sy, sw, sh;        // visible part of the texture
    float cr, cg, cb, ca;      // color multiplier

    void drawRegion(int srcX, int srcY, int srcW, int srcH,
                    float dx, float dy, float dw, float dh) const;
};

class CL_Sprite
{
public:
    CL_Sprite(const std::string& resourceName, CL_ResourceManager* resources);
    ~CL_Sprite();

    int get_width() const;
    int get_height() const;
    int get_num_frames() const { return (int)frames.size(); }
    int get_frame_count() const { return (int)frames.size(); }
    int get_current_frame() const { return current; }

    void set_frame(int frame);
    //! Advances the animation by the time since the last call.
    void update();

    void draw(float x, float y) const;

private:
    struct Frame { int x, y, w, h; };
    std::shared_ptr<CL1Texture> tex;
    std::vector<Frame> frames;
    int   current;
    int   delay;               // ms per frame
    bool  loop;
    float timeAcc;
    unsigned int lastUpdate;
};

class CL_Font
{
public:
    CL_Font(const std::string& resourceName, CL_ResourceManager* resources);
    ~CL_Font();

    void set_alignment(clan::Origin origin, int dx = 0, int dy = 0);
    void draw(float x, float y, const std::string& text) const;
    int  get_width(const std::string& text) const;
    int  get_height() const;

private:
    struct Glyph { int x, w; bool valid; };
    std::shared_ptr<CL1Texture> tex;
    Glyph glyphs[256];
    int   glyphTop, glyphHeight;
    int   spaceLen;
    clan::Origin alignment;
    int   alignDx, alignDy;
};

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

class CL_SoundBuffer : public clan::SoundBuffer
{
public:
    CL_SoundBuffer(const std::string& resourceName, CL_ResourceManager* resources);
};

// ---------------------------------------------------------------------------
// Display, window, input, system
// ---------------------------------------------------------------------------

class CL_InputContext {};

class CL_DisplayWindow
{
public:
    //! Creates the window and the drawing canvas. Throws CL_Error on failure.
    CL_DisplayWindow(const std::string& title, int width, int height, bool fullscreen = false);
    ~CL_DisplayWindow();

    CL_InputContext* get_ic() { return &inputContext; }
    void set_fullscreen(int width, int height);
    void set_windowed();
    void set_size(int width, int height);

private:
    CL_InputContext inputContext;
};

class CL_Display
{
public:
    static void flip();
    static void clear();
    static void fill_rect(const CL_Rect& rect, const CL_Color& color);
    static void draw_line(int x1, int y1, int x2, int y2, const CL_Color& color);
    static void set_cliprect(const CL_Rect& rect);
};

class CL_Slot
{
public:
    CL_Slot() : id(0) {}
    int id;
};

//! Replacement for the old keyboard signals.
class CL1KeySignal
{
public:
    typedef std::function<void(const CL_InputEvent&)> Handler;

    template<class T, class U>
    CL_Slot connect(T* object, void (U::*method)(const CL_InputEvent&))
    {
        return add([object, method](const CL_InputEvent& e) { (object->*method)(e); });
    }

    void disconnect(const CL_Slot& slot);
    void fire(const CL_InputEvent& e);

private:
    CL_Slot add(Handler handler);
    std::vector<std::pair<int, Handler> > handlers;
    int nextId = 1;
};

class CL_Keyboard
{
public:
    static bool get_keycode(int keycode);
    static CL1KeySignal& sig_key_up();
};

class CL_Mouse
{
public:
    static int  get_x();
    static int  get_y();
    static bool get_keycode(int keycode);
    static void show();
    static void hide();
};

class CL_System
{
public:
    //! Milliseconds since the program started.
    static unsigned int get_time();
    //! Processes window / input events. Call regularly.
    static void keep_alive();
};

// Debug console: not available, calls do nothing.
class CL_ConsoleWindow
{
public:
    CL_ConsoleWindow(const std::string&, int = 80, int = 160) {}
    void redirect_stdio() {}
    void wait_for_key() {}
};

class CL_DirectoryScanner
{
public:
    //! Returns true if 'path' exists and is a directory.
    bool scan(const std::string& path);
};

class CL_Directory
{
public:
    static bool create(const std::string& path);
};

// ---------------------------------------------------------------------------
// Application entry point
// ---------------------------------------------------------------------------

//! Base class of the application (the old ClanLib main() mechanism). One global
//! instance of a derived class must exist; its main() is called at startup.
class CL_ClanApplication
{
public:
    CL_ClanApplication();
    virtual ~CL_ClanApplication() {}
    virtual int main(int argc, char** argv) = 0;
};

#endif
