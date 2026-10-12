void *coverage_chandle(int index) {
  static int values[2];
  return &values[index & 1];
}
