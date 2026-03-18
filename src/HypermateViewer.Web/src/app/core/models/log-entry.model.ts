export interface LogEntry {
  line: string;
  machine: string;
  stepDescription: string;
  oldState: string;
  newState: string;
  oldErpCode: string;
  newErpCode: string;
}
