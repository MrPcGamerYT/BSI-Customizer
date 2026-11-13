.pragma library

function initialGeometry(screen) {
    var screenWidth = Math.max(1, Number(screen.width) || 1)
    var screenHeight = Math.max(1, Number(screen.height) || 1)
    var inset = Math.min(24, Math.floor(Math.min(screenWidth, screenHeight) * 0.03))
    var usableWidth = Math.max(1, screenWidth - inset * 2)
    var usableHeight = Math.max(1, screenHeight - inset * 2)
    var maxWidth = Math.min(1920, usableWidth * 0.90)
    var maxHeight = Math.min(1180, usableHeight * 0.90)
    var aspect = Math.max(1.25, Math.min(1.60, usableWidth / usableHeight))
    var width = Math.min(maxWidth, maxHeight * aspect)
    var height = width / aspect
    if (height > maxHeight) {
        height = maxHeight
        width = height * aspect
    }
    width = Math.max(1, Math.floor(width))
    height = Math.max(1, Math.floor(height))
    return {
        x: Math.round((Number(screen.x) || 0) + (screenWidth - width) / 2),
        y: Math.round((Number(screen.y) || 0) + (screenHeight - height) / 2),
        width: width,
        height: height,
        minimumWidth: Math.max(1, Math.min(980, Math.floor(width * 0.88))),
        minimumHeight: Math.max(1, Math.min(640, Math.floor(height * 0.86)))
    }
}
