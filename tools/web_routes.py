#!/usr/bin/env python3
"""Route table scanner for the NNode web UI.

Understands just enough JavaScript to pull out objects shaped like
  { id: "status", pages: ["overview", ...] }
and skips everything else. Raises ValueError on malformed input.
"""


def _skip_ws_comments(text, i):
    n = len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n\v\f":
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i + 2)
            i = n if j < 0 else j + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                return n
            i = j + 2
        else:
            break
    return i


def _parse_string(text, i):
    quote = text[i]
    i += 1
    buf = []
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\" and i + 1 < n:
            buf.append(text[i + 1])
            i += 2
            continue
        if c == quote:
            return "".join(buf), i + 1
        buf.append(c)
        i += 1
    raise ValueError("unterminated string literal")


def _parse_ident(text, i):
    j = i
    while j < len(text) and (text[j].isalnum() or text[j] in "_$"):
        j += 1
    if j == i:
        raise ValueError("expected identifier")
    return text[i:j], j


def _skip_value(text, i):
    """Skip one JavaScript value starting at i, return the index after it."""
    i = _skip_ws_comments(text, i)
    if i >= len(text):
        raise ValueError("unexpected end of input")
    c = text[i]
    if c in "\"'":
        _, i = _parse_string(text, i)
        return i
    if c in "[{":
        opener = c
        closer = "]" if c == "[" else "}"
        depth = 1
        i += 1
        while i < len(text) and depth > 0:
            ch = text[i]
            if ch in "\"'":
                _, i = _parse_string(text, i)
                continue
            if ch == "/" and i + 1 < len(text) and text[i + 1] in "/*":
                i = _skip_ws_comments(text, i)
                continue
            if ch == opener:
                depth += 1
            elif ch == closer:
                depth -= 1
            i += 1
        if depth != 0:
            raise ValueError("unbalanced brackets")
        return i
    while i < len(text) and text[i] not in ",}]":
        if text[i] in "\"'":
            _, i = _parse_string(text, i)
            continue
        i += 1
    return i


def _try_parse_route_object(text, i):
    """Parse one {...} at i. Return ((id, [pages]) or None, index after })."""
    j = _skip_ws_comments(text, i + 1)
    rid = None
    pages = None
    while True:
        j = _skip_ws_comments(text, j)
        if j < len(text) and text[j] == "}":
            if rid is None or pages is None:
                return None, j + 1
            return (rid, pages), j + 1
        if j < len(text) and text[j] in "\"'":
            key, j = _parse_string(text, j)
        else:
            key, j = _parse_ident(text, j)
        j = _skip_ws_comments(text, j)
        if j >= len(text) or text[j] != ":":
            raise ValueError("not a route object")
        j = _skip_ws_comments(text, j + 1)
        if key == "id":
            if j >= len(text) or text[j] not in "\"'":
                raise ValueError("route id is not a string")
            rid, j = _parse_string(text, j)
        elif key == "pages":
            if j >= len(text) or text[j] != "[":
                raise ValueError("route pages is not an array")
            j += 1
            items = []
            while True:
                j = _skip_ws_comments(text, j)
                if j < len(text) and text[j] == "]":
                    j += 1
                    break
                if j >= len(text) or text[j] not in "\"'":
                    raise ValueError("route page is not a string")
                value, j = _parse_string(text, j)
                items.append(value)
                j = _skip_ws_comments(text, j)
                if j < len(text) and text[j] == ",":
                    j += 1
                elif j < len(text) and text[j] == "]":
                    j += 1
                    break
                else:
                    raise ValueError("malformed pages array")
            pages = items
        else:
            j = _skip_value(text, j)
        j = _skip_ws_comments(text, j)
        if j < len(text) and text[j] == ",":
            j += 1
        elif j < len(text) and text[j] == "}":
            if rid is None or pages is None:
                return None, j + 1
            return (rid, pages), j + 1
        else:
            raise ValueError("not a route object")


def _skip_template(text, i):
    """Skip a `...` template literal starting at i, return index after it."""
    assert text[i] == "`"
    i += 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\" and i + 1 < n:
            i += 2
            continue
        if c == "`":
            return i + 1
        if c == "$" and i + 1 < n and text[i + 1] == "{":
            i = _skip_value(text, i + 1)
            continue
        i += 1
    raise ValueError("unterminated template literal")


def find_routes(js):
    """Return [(section_id, [pages])] for every route object in the script."""
    routes = []
    i = 0
    n = len(js)
    while i < n:
        i = _skip_ws_comments(js, i)
        if i >= n:
            break
        c = js[i]
        if c in "\"'":
            try:
                _, i = _parse_string(js, i)
            except ValueError:
                i += 1
            continue
        if c == "`":
            try:
                i = _skip_template(js, i)
            except ValueError:
                i += 1
            continue
        if c != "{":
            i += 1
            continue
        try:
            found, j = _try_parse_route_object(js, i)
        except (ValueError, IndexError):
            i += 1  # not a route-shaped object, keep scanning
            continue
        if found is not None:
            routes.append(found)
        i = j
    return routes
