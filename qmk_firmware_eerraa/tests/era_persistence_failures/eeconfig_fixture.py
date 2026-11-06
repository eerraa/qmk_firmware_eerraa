"""Keep minimal headers linked to the production semantic acceptance API."""


def semantic_api(header):
    start = header.index("#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE\n")
    end = header.index("#endif", start) + len("#endif")
    return header[start:end] + "\n"
