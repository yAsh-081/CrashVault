export function signalName(signal: number): string {
  switch (signal) {
    case 11:
      return "SIGSEGV";
    case 6:
      return "SIGABRT";
    case 8:
      return "SIGFPE";
    case 4:
      return "SIGILL";
    default:
      return `SIG${signal}`;
  }
}

export function signalClass(signal: number): string {
  switch (signal) {
    case 11:
      return "signal-segv";
    case 6:
      return "signal-abrt";
    case 8:
      return "signal-fpe";
    case 4:
      return "signal-ill";
    default:
      return "signal-other";
  }
}

export function formatTimestamp(unix: number | null | undefined): string {
  if (!unix) {
    return "—";
  }
  return new Date(unix * 1000).toLocaleString();
}

export function formatRelative(unix: number | null | undefined): string {
  if (!unix) {
    return "—";
  }
  const diff = Math.max(0, Math.floor(Date.now() / 1000) - unix);
  if (diff < 60) {
    return `${diff}s ago`;
  }
  if (diff < 3600) {
    return `${Math.floor(diff / 60)}m ago`;
  }
  if (diff < 86400) {
    return `${Math.floor(diff / 3600)}h ago`;
  }
  return `${Math.floor(diff / 86400)}d ago`;
}

export function formatHex(value: number | null | undefined, width = 16): string {
  if (value === null || value === undefined) {
    return "—";
  }
  return `0x${value.toString(16).padStart(width, "0")}`;
}

export function truncate(text: string, max = 48): string {
  if (text.length <= max) {
    return text;
  }
  return `${text.slice(0, max - 1)}…`;
}

export function displayFunction(name: string | null | undefined): string {
  if (!name || name === "??") {
    return "Unknown symbol";
  }
  return name;
}

export function isUnknownSymbol(name: string | null | undefined): boolean {
  return !name || name === "??";
}
