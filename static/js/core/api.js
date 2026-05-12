async function parseJsonResponse(response) {
    const raw = await response.text();
    try {
        return JSON.parse(raw);
    } catch (error) {
        console.error("Invalid JSON response", {
            url: response.url,
            status: response.status,
            body: raw
        });
        throw new Error("Server returned an invalid response.");
    }
}

export async function postForm(url, form) {
    const body = new URLSearchParams(new FormData(form));
    const response = await fetch(url, {
        method: "POST",
        headers: {"Content-Type": "application/x-www-form-urlencoded"},
        body
    });
    const payload = await parseJsonResponse(response);
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
    const payload = await parseJsonResponse(response);
    if (!response.ok) {
        throw new Error(payload.error || payload.output || "Request failed");
    }
    return payload;
}

export async function requestJson(url, options = {}) {
    const response = await fetch(url, options);
    const payload = await parseJsonResponse(response);
    if (!response.ok) {
        throw new Error(payload.error || payload.output || "Request failed");
    }
    return payload;
}
