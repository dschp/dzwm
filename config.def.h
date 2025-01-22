/* See LICENSE file for copyright and license details. */

#define BAR_CLIENT_MIN_WIDTH 200

/* appearance */
static const unsigned int borderpx  = 1;        /* border pixel of windows */
static const unsigned int snap      = 16;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 0;        /* 0 means bottom bar */
static const char *fonts[]          = { "sans-serif:size=7" };
static const char dmenufont[]       = "sans-serif:size=11";
static const char col_sel1[]        = "#00bbff";
static const char col_sel2[]        = "#f32f7c";
static const char col_sel3[]        = "#afff00";
static const char col_sel4[]        = "#ffcc35";
static const char col_sel5[]        = "#be2df1";
static const char col_col6[]        = "#00f2ae";
static const char col_white1[]      = "#ffffff";
static const char col_white2[]      = "#eeeeee";
static const char col_gray[]        = "#888888";
static const char col_black1[]      = "#000000";
static const char col_black2[]      = "#191919";
static const char col_black3[]      = "#2a2a2a";
static const char col_red[]         = "#f32f7c";
static const char col_green[]       = "#afff00";
static const char col_blue[]        = "#00bbff";
static const char col_yellow[]      = "#ffff00";
static const char col_bdr0[]        = "#242424";
static const char col_bdr1[]        = "#0090ff";
static const char col_bdr2[]        = "#c01770";
static const char col_bdr3[]        = "#7fb900";
static const char col_bdr4[]        = "#f2b500";
static const char col_bdr5[]        = "#be2df1";
static const char col_bdr6[]        = "#00f2ae";
static const char col_dmenu_selbg[] = "#0077cc";


static const char *colors[][3]      = {
  /*                   fg            bg            border   */
  [SchemeNorm]     = { col_white2,   col_black2,   col_bdr0 },
  [SchemeNoClient] = { col_gray,     col_black2,   col_bdr0 },
  [SchemeSel1]     = { col_sel1,     col_black1,   col_bdr1 },
  [SchemeSel2]     = { col_sel2,     col_black1,   col_bdr2 },
  [SchemeSel3]     = { col_sel3,     col_black1,   col_bdr3 },
  [SchemeSel4]     = { col_sel4,     col_black1,   col_bdr4 },
  [SchemeSel5]     = { col_sel5,     col_black1,   col_bdr5 },
  [SchemeSel6]     = { col_col6,     col_black1,   col_bdr6 },
  [SchemeSel7]     = { col_sel1,     col_black1,   col_bdr1 },
  [SchemeSel8]     = { col_sel2,     col_black1,   col_bdr2 },
  [SchemeSel9]     = { col_sel3,     col_black1,   col_bdr3 },
  [SchemeSel10]    = { col_sel4,     col_black1,   col_bdr4 },
  [SchemeSel11]    = { col_sel5,     col_black1,   col_bdr5 },
  [SchemeSel12]    = { col_col6,     col_black1,   col_bdr6 },
  [SchemeWS]       = { col_green,    col_black1,   col_bdr0 },
  [SchemeStats]    = { col_white2,   col_black3,   col_bdr0 },
  [SchemeDivRatio] = { col_yellow,   col_black3,   col_bdr0 },
  [SchemeBarInfo]  = { col_white2,   col_black1,   col_bdr0 },
  [SchemeDate1]    = { col_yellow,   col_black1,   col_bdr0 },
  [SchemeDate2]    = { col_green,    col_black1,   col_bdr0 },
  [SchemeDate3]    = { col_red,      col_black1,   col_bdr0 },
  [SchemeDate4]    = { col_blue,     col_black1,   col_bdr0 },
};

static const char *wsnames[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=",
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "\\",
  "A", "S", "D", "F", "G", ";", "'",
  "Z", "X", "C", "V", "B", "N", "M", "<", ">", "/", "~",
};

static const char *altnames[] = {
  "", "alt ", "ctrl "
};

static const char *panenames[] = {
  "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10" ,"f11", "f12"
};

/* layout(s) */
static const uint div_ratio_init = 70;   /* 55 means 0.55 [0.05..0.95] */
static const uint max_disp_init  = 0;    /* num of max clients. 0 means showing all clients */
static const int resizehints     = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen  = 1;    /* 1 will force focus on the fullscreen window */

static const Layout layouts[] = {
  /* symbol   arrange function */
  { "v",  tile_v },
  { "h",  tile_h },
};

/* key definitions */
#define MODKEY  Mod4Mask
#define AltMask Mod1Mask
#define ACMask  AltMask|ControlMask
#define ASMask  AltMask|ShiftMask
#define CSMask  ControlMask|ShiftMask
#define ACSMask AltMask|ControlMask|ShiftMask

#define WSKEYS(KEY,IDX)							\
  { MODKEY,              KEY,  switchworkspace,  {.ws = {IDX, 0}} },	\
  { MODKEY|AltMask,      KEY,  switchworkspace,  {.ws = {IDX, 1}} },	\
  { MODKEY|ControlMask,  KEY,  switchworkspace,  {.ws = {IDX, 2}} },	\
  { MODKEY|ShiftMask,    KEY,  moveclient_ws,    {.ui = IDX} }

#define FNKEYS(KEY,IDX)							\
  { MODKEY,              KEY,  togglepane,         {.ui = IDX} },	\
  { MODKEY|ShiftMask,    KEY,  moveclient_paneidx, {.ui = IDX - 1} },	\
  { MODKEY|ControlMask,  KEY,  set_bar_status,     {.ui = IDX - 1} }

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = {
  "mydmenu",
  "-m",   dmenumon,
  "-fn",  dmenufont,
  "-nb",  col_black2,
  "-nf",  col_white1,
  "-sb",  col_dmenu_selbg,
  "-sf",  col_white2,
  NULL };
static const char *termcmd[]  = { "st", NULL };

static Key keys[] = {
  /* modifier              key              function              argument */
  { MODKEY|ACSMask,        XK_BackSpace,    quit,                 {0} },
  { MODKEY|AltMask,        XK_Delete,       killclient,           {0} },
  { MODKEY,                XK_Return,       spawn,                {.v = dmenucmd } },
  { MODKEY|ShiftMask,      XK_Return,       spawn,                {.v = termcmd } },
  { MODKEY|ControlMask,    XK_Return,       zoom,                 {0} },
  { MODKEY,                XK_Escape,       togglepane,           {.ui = 0} },
  { MODKEY|ControlMask,    XK_Escape,       clearpanes,           {0} },
  { MODKEY,                XK_Tab,          switchworkspace,      {.ws = {-1, -1} } },
  { MODKEY,                XK_space,        togglefloating,       {0} },
  { MODKEY|AltMask,        XK_space,        togglebar,            {0} },
  { MODKEY|ControlMask,    XK_space,        centerwindow,         {0} },
  { MODKEY|ShiftMask,      XK_space,        maximize,             {0} },
  { MODKEY,                XK_Up,           inc_max_disp,         {.i = +1 } },
  { MODKEY,                XK_Down,         inc_max_disp,         {.i = -1 } },
  { MODKEY,                XK_Left,         cycle_layout,         {.i = -1 } },
  { MODKEY,                XK_Right,        cycle_layout,         {.i = +1 } },
  { MODKEY,                XK_h,            focuspane,            {.i = -1 } },
  { MODKEY,                XK_j,            cycle_focus,          {.i = +1 } },
  { MODKEY,                XK_k,            cycle_focus,          {.i = -1 } },
  { MODKEY,                XK_l,            focuspane,            {.i = +1 } },
  { MODKEY|ShiftMask,      XK_h,            moveclient_pane,      {.i = -1 } },
  { MODKEY|ShiftMask,      XK_j,            movestack,            {.i = +1 } },
  { MODKEY|ShiftMask,      XK_k,            movestack,            {.i = -1 } },
  { MODKEY|ShiftMask,      XK_l,            moveclient_pane,      {.i = +1 } },
  { MODKEY|ControlMask,    XK_h,            focuspane_showing,    {.i = -1 } },
  { MODKEY|ControlMask,    XK_j,            cycle_bar_status,     {.i = +1 } },
  { MODKEY|ControlMask,    XK_k,            cycle_bar_status,     {.i = -1 } },
  { MODKEY|ControlMask,    XK_l,            focuspane_showing,    {.i = +1 } },
  { MODKEY|AltMask,        XK_h,            inc_div_ratio,        {.i = -5 } },
  { MODKEY|AltMask,        XK_j,            inc_div_ratio,        {.i = +1 } },
  { MODKEY|AltMask,        XK_k,            inc_div_ratio,        {.i = -1 } },
  { MODKEY|AltMask,        XK_l,            inc_div_ratio,        {.i = +5 } },
  WSKEYS(                  XK_1,             0),
  WSKEYS(                  XK_2,             1),
  WSKEYS(                  XK_3,             2),
  WSKEYS(                  XK_4,             3),
  WSKEYS(                  XK_5,             4),
  WSKEYS(                  XK_6,             5),
  WSKEYS(                  XK_7,             6),
  WSKEYS(                  XK_8,             7),
  WSKEYS(                  XK_9,             8),
  WSKEYS(                  XK_0,             9),
  WSKEYS(                  XK_minus,        10),
  WSKEYS(                  XK_equal,        11),
  WSKEYS(                  XK_q,            12),
  WSKEYS(                  XK_w,            13),
  WSKEYS(                  XK_e,            14),
  WSKEYS(                  XK_r,            15),
  WSKEYS(                  XK_t,            16),
  WSKEYS(                  XK_y,            17),
  WSKEYS(                  XK_u,            18),
  WSKEYS(                  XK_i,            19),
  WSKEYS(                  XK_o,            20),
  WSKEYS(                  XK_p,            21),
  WSKEYS(                  XK_bracketleft,  22),
  WSKEYS(                  XK_bracketright, 23),
  WSKEYS(                  XK_backslash,    24),
  WSKEYS(                  XK_a,            25),
  WSKEYS(                  XK_s,            26),
  WSKEYS(                  XK_d,            27),
  WSKEYS(                  XK_f,            28),
  WSKEYS(                  XK_g,            29),
  WSKEYS(                  XK_semicolon,    30),
  WSKEYS(                  XK_apostrophe,   31),
  WSKEYS(                  XK_z,            32),
  WSKEYS(                  XK_x,            33),
  WSKEYS(                  XK_c,            34),
  WSKEYS(                  XK_v,            35),
  WSKEYS(                  XK_b,            36),
  WSKEYS(                  XK_n,            37),
  WSKEYS(                  XK_m,            38),
  WSKEYS(                  XK_comma,        39),
  WSKEYS(                  XK_period,       40),
  WSKEYS(                  XK_slash,        41),
  WSKEYS(                  XK_grave,        42),
  FNKEYS(                  XK_F1,            1),
  FNKEYS(                  XK_F2,            2),
  FNKEYS(                  XK_F3,            3),
  FNKEYS(                  XK_F4,            4),
  FNKEYS(                  XK_F5,            5),
  FNKEYS(                  XK_F6,            6),
  FNKEYS(                  XK_F7,            7),
  FNKEYS(                  XK_F8,            8),
  FNKEYS(                  XK_F9,            9),
  FNKEYS(                  XK_F10,          10),
  FNKEYS(                  XK_F11,          11),
  FNKEYS(                  XK_F12,          12),
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
  /* click                 event mask      button          function        argument */
  { ClkClientWin,          MODKEY,         Button1,        movemouse,      {0} },
  { ClkClientWin,          MODKEY,         Button3,        resizemouse,    {0} },
};

void barstatus_datetime_est(Monitor *m) {
  barstatus_datetime(m, "EST", ":EST");
}
void barstatus_datetime_ict(Monitor *m) {
  barstatus_datetime(m, "ICT", ":Asia/Bangkok");
}
void barstatus_datetime_jst(Monitor *m) {
  barstatus_datetime(m, "JST", ":Asia/Tokyo");
}
void barstatus_datetime_utc(Monitor *m) {
  barstatus_datetime(m, "UTC", ":UTC");
}

void barstatus_test1(Monitor *m) {
  barstatus_command(m, "cat ~/dzwm-status.txt");
}

void barstatus_test2(Monitor *m) {
  barstatus_file(m, "dzwm-status.txt");
}

static const StatusRenderSpec statusrenderspecs[] = {
  {"🪟", barstatus_wintitle,      0},
  {"📌", barstatus_workspaces,    0},
  {"📆", barstatus_datetime_ict,  0},
  {"📆", barstatus_datetime_est,  0},
  {"📆", barstatus_datetime_utc,  0},
  {"📆", barstatus_datetime_jst,  0},
  {"📃", barstatus_test1,         5},
  {"🔖", barstatus_test2,         5},
  {"💥", barstatus_dummy,        99},
  {"💫", barstatus_dummy,        99},
  {"💦", barstatus_dummy,        99},
  {"🚀", barstatus_dummy,        99},
  {"🔥", barstatus_dummy,        99},
  {"💧", barstatus_dummy,        99},
  {"💡", barstatus_dummy,        99},
  {"💬", barstatus_dummy,        99},
  {"🎃", barstatus_dummy,        99},
  {"🔋", barstatus_dummy,        99},
  {"🔊", barstatus_dummy,        99},
  {"📢", barstatus_dummy,        99},
  {"📝", barstatus_dummy,        99},
  {"⚙️", barstatus_dummy,        99},
};
