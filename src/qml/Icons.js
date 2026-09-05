.pragma library

// Minimal inline SVG icon set (lucide-style paths, no external files).

function icon(inner, color) {
    var svg = "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'"
            + " fill='none' stroke='" + color + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
            + inner + "</svg>";
    return "data:image/svg+xml," + encodeURIComponent(svg);
}

var dashboard = "<rect x='3' y='3' width='7' height='7' rx='1.5'/>"
              + "<rect x='14' y='3' width='7' height='7' rx='1.5'/>"
              + "<rect x='14' y='14' width='7' height='7' rx='1.5'/>"
              + "<rect x='3' y='14' width='7' height='7' rx='1.5'/>";

var theme = "<circle cx='13.5' cy='6.5' r='1.5'/><circle cx='17.5' cy='10.5' r='1.5'/>"
          + "<circle cx='8.5' cy='7.5' r='1.5'/><circle cx='6.5' cy='12.5' r='1.5'/>"
          + "<path d='M12 2a10 10 0 0 0 0 20c1.1 0 2-.9 2-2 0-.5-.2-1-.5-1.3-.3-.4-.5-.8-.5-1.2"
          + " 0-1 .8-1.8 1.8-1.8H16a6 6 0 0 0 6-6c0-4.9-4.5-7.7-10-7.7z'/>";

var svg = "<rect x='3' y='3' width='18' height='18' rx='2'/>"
        + "<circle cx='9' cy='9' r='2'/>"
        + "<path d='m21 15-3.09-3.09a2 2 0 0 0-2.82 0L6 21'/>";

var fps = "<path d='M12 14 8.5 9.5'/>"
        + "<path d='M20.5 16a9 9 0 1 0-17 0'/>"
        + "<circle cx='12' cy='14' r='1'/>";

var integrity = "<path d='M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z'/>"
              + "<path d='m9 12 2 2 4-4'/>";

var boot = "<path d='m22 8-6 4 6 4z'/>"
         + "<rect x='2' y='6' width='14' height='12' rx='2'/>";

var extras = "<path d='M12 3v18'/><path d='M3 12h18'/>"
           + "<path d='m5.6 5.6 12.8 12.8'/><path d='m18.4 5.6-12.8 12.8'/>";

var appearance = theme;

var settings = "<circle cx='12' cy='12' r='3'/>"
             + "<path d='M19.4 15a1.7 1.7 0 0 0 .34 1.87l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06"
             + "a1.7 1.7 0 0 0-1.87-.34 1.7 1.7 0 0 0-1.03 1.56V21a2 2 0 1 1-4 0v-.09"
             + "a1.7 1.7 0 0 0-1.03-1.56 1.7 1.7 0 0 0-1.87.34l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06"
             + "a1.7 1.7 0 0 0 .34-1.87 1.7 1.7 0 0 0-1.56-1.03H3a2 2 0 1 1 0-4h.09"
             + "a1.7 1.7 0 0 0 1.56-1.03 1.7 1.7 0 0 0-.34-1.87l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06"
             + "a1.7 1.7 0 0 0 1.87.34h.09a1.7 1.7 0 0 0 1.03-1.56V3a2 2 0 1 1 4 0v.09"
             + "a1.7 1.7 0 0 0 1.03 1.56 1.7 1.7 0 0 0 1.87-.34l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06"
             + "a1.7 1.7 0 0 0-.34 1.87v.09a1.7 1.7 0 0 0 1.56 1.03H21a2 2 0 1 1 0 4h-.09"
             + "a1.7 1.7 0 0 0-1.56 1.03z'/>";

var about = "<circle cx='12' cy='12' r='10'/>"
          + "<path d='M12 16v-4'/><path d='M12 8h.01'/>";

var refresh = "<path d='M21 12a9 9 0 1 1-2.64-6.36'/>"
            + "<path d='M21 3v6h-6'/>";

// YouTube logo (lucide 'youtube'): rounded TV shape + play triangle.
var youtube = "<path d='M2.5 17a24.12 24.12 0 0 1 0-10 2 2 0 0 1 1.4-1.4 49.56 49.56 0 0 1 16.2 0A2 2 0 0 1 21.5 7a24.12 24.12 0 0 1 0 10 2 2 0 0 1-1.4 1.4 49.55 49.55 0 0 1-16.2 0A2 2 0 0 1 2.5 17'/>"
            + "<path d='m10 15 5-3-5-3z'/>";
