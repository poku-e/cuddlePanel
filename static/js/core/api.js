export async function postForm(url, form) {
    const body = new URLSearchParams(new FormData(form));
    const response = await fetch(url, {
        method: "POST",
        headers: {"Content-Type": "application/x-www-form-urlencoded"},
        body
    });
    const payload = await response.json();
    if (!response.ok) {
        throw new Error(payload.error || payload.output || "Request failed");
    }
    return payload;
}

export async function postParams(url, params) {
    const body = new URLSearchParams(params);
    const response = await fetch(url, {
        method: "POST",
        headers: {"Content-Type": "application/x-www-form-urlencoded"},
        body
    });
    const payload = await response.json();
    if (!response.ok) {
        throw new Error(payload.error || payload.output || "Request failed");
    }
    return payload;
}

export async function requestJson(url, options = {}) {
    const response = await fetch(url, options);
    const payload = await response.json();
    if (!response.ok) {
        throw new Error(payload.error || payload.output || "Request failed");
    }
    return payload;
}
