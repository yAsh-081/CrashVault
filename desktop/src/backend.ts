import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

// Types shared with the Tauri backend (serde field names match Rust)

export interface DashboardSummary {
  crashes_today: number;
  unique_groups: number;
  total_occurrences: number;
  affected_versions: number;
}

export interface FrequentCrash {
  id: number;
  signal: number;
  signal_name: string;
  top_function: string;
  application: string;
  occurrence_count: number;
  last_seen: number;
}

export interface RecentActivity {
  occurrence_id: number;
  group_id: number;
  signal: number;
  signal_name: string;
  top_function: string;
  application: string;
  imported_at: number;
}

export interface TrendPoint {
  day: string;
  count: number;
}

export interface DashboardData {
  summary: DashboardSummary;
  frequent_crashes: FrequentCrash[];
  recent_activity: RecentActivity[];
  trend: TrendPoint[];
}

export interface CrashGroupSummary {
  id: number;
  signal: number;
  signal_name: string;
  top_function: string;
  top_module: string | null;
  application: string;
  versions: string[];
  occurrence_count: number;
  first_seen: number;
  last_seen: number;
}

export interface CrashGroupFilters {
  application?: string | null;
  signal?: number | null;
  search?: string | null;
}

export interface VersionCount {
  version: string;
  count: number;
}

export interface CrashGroupDetail {
  id: number;
  signal: number;
  signal_name: string;
  top_function: string;
  top_module: string | null;
  application: string;
  fingerprint: string;
  occurrence_count: number;
  first_seen: number;
  last_seen: number;
  versions: VersionCount[];
  occurrences: CrashOccurrenceSummary[];
}

export interface CrashOccurrenceSummary {
  id: number;
  imported_at: number;
  crash_time_sec: number | null;
  app_version: string | null;
  pid: number | null;
  tid: number | null;
  fault_addr: number | null;
}

export interface StackFrame {
  frame_index: number;
  raw_address: number;
  normalized_address: number | null;
  module: string | null;
  function_name: string;
  source_file: string | null;
  source_line: number | null;
  symbol_status: string;
}

export interface RegisterSet {
  rip: number | null;
  rsp: number | null;
  rbp: number | null;
  rax: number | null;
  rbx: number | null;
  rcx: number | null;
  rdx: number | null;
  rsi: number | null;
  rdi: number | null;
  r8: number | null;
  r9: number | null;
  r10: number | null;
  r11: number | null;
  r12: number | null;
  r13: number | null;
  r14: number | null;
  r15: number | null;
}

export interface OccurrenceDetail {
  id: number;
  group_id: number;
  imported_at: number;
  crash_time_sec: number | null;
  app_version: string | null;
  pid: number | null;
  tid: number | null;
  fault_addr: number | null;
  executable_path: string | null;
  executable_base: number | null;
  registers: RegisterSet;
  frames: StackFrame[];
}

export interface ApplicationSummary {
  name: string;
  group_count: number;
  occurrence_count: number;
  last_crash: number | null;
}

export interface AppSettings {
  crash_directory: string;
  auto_import: boolean;
  processor_path: string | null;
}

export interface StorageStatus {
  crash_directory: string;
  database_path: string;
  pending_reports: number;
  processed_reports: number;
  database_exists: boolean;
}

export interface ImportStats {
  scanned: number;
  imported: number;
  rejected: number;
  duplicate: number;
}

export interface WatcherStatus {
  watching: boolean;
  auto_import: boolean;
  last_import_at: number | null;
  last_import_stats: ImportStats | null;
  last_error: string | null;
}

export async function getDashboard(): Promise<DashboardData> {
  return invoke("get_dashboard");
}

export async function getCrashGroups(
  filters: CrashGroupFilters,
): Promise<CrashGroupSummary[]> {
  return invoke("get_crash_groups", { filters });
}

export async function getCrashGroupDetail(id: number): Promise<CrashGroupDetail> {
  return invoke("get_crash_group_detail", { id });
}

export async function getOccurrenceDetail(id: number): Promise<OccurrenceDetail> {
  return invoke("get_occurrence_detail", { id });
}

export async function getApplications(): Promise<ApplicationSummary[]> {
  return invoke("get_applications");
}

export async function getSettings(): Promise<AppSettings> {
  return invoke("get_settings");
}

export async function setSettings(settings: AppSettings): Promise<void> {
  return invoke("set_settings", { settings });
}

export async function getStorageStatus(): Promise<StorageStatus> {
  return invoke("get_storage_status");
}

export async function processPendingReports(): Promise<ImportStats> {
  return invoke("process_pending_reports");
}

export async function getWatcherStatus(): Promise<WatcherStatus> {
  return invoke("get_watcher_status");
}

export function onImportComplete(
  handler: (stats: ImportStats) => void,
): Promise<() => void> {
  return listen<ImportStats>("import-complete", (event) => {
    handler(event.payload);
  });
}
