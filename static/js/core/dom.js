export function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

export async function loadPartial(targetId, path) {
    const response = await fetch(path);
    document.getElementById(targetId).innerHTML = await response.text();
}
