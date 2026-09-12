bool decodePredictiveTopology(
    std::uint32_t symbols,
    std::uint32_t faces,
    std::uint32_t encoded_vertices,
    std::uint32_t split_count,
    const std::vector<SplitEvent>& splits,
    PredictiveTraversal& traversal,
    CornerTable *table,
    std::string *error)
{
    if (!table || encoded_vertices > UINT32_MAX - split_count ||
        !table->reset(faces, encoded_vertices + split_count))
        return fail(error, "invalid Draco EdgeBreaker table size");

    traversal.valence.assign(static_cast<std::size_t>(encoded_vertices + split_count), 0);
    std::vector<int> active;
    std::vector<int> isolated;
    std::unordered_map<int, int> split_active;
    std::size_t split_cursor = 0u;
    int decoded_faces = 0;

    for (std::uint32_t sid = 0u; sid < symbols; ++sid) {
        std::uint32_t symbol = 0u;
        if (!traversal.next(&symbol)) return fail(error, "invalid Draco predictive symbol stream");
        const int corner = decoded_faces++ * 3;
        bool check_split = false;
        int merge_dest = -1;
        int merge_source = -1;

        if (symbol == 0u) {
            if (active.empty()) return fail(error, "Draco C symbol has no active edge");
            const int a = active.back();
            const int vx = table->vertex(table->next(a));
            const int b = table->next(table->leftMost(vx));
            if (a == b || a < 0 || b < 0 || table->oppositeCorner(a) >= 0 || table->oppositeCorner(b) >= 0)
                return fail(error, "invalid Draco C topology");
            const int va = table->vertex(table->previous(a));
            const int vb = table->vertex(table->next(b));
            if (vx == va || vx == vb || !table->setOpposite(a, corner + 1) ||
                !table->setOpposite(b, corner + 2) || !table->map(corner, vx) ||
                !table->map(corner + 1, vb) || !table->map(corner + 2, va))
                return fail(error, "invalid Draco C mapping");
            table->setLeftmost(va, corner + 2);
            active.back() = corner;
        } else if (symbol == 3u || symbol == 5u) {
            if (active.empty()) return fail(error, "Draco L/R symbol has no active edge");
            const int a = active.back();
            if (table->oppositeCorner(a) >= 0) return fail(error, "occupied Draco active edge");
            int opposite = 0;
            int left = 0;
            int right = 0;
            if (symbol == 5u) {
                opposite = corner + 2;
                left = corner + 1;
                right = corner;
            } else {
                opposite = corner + 1;
                left = corner;
                right = corner + 2;
            }
            if (!table->setOpposite(opposite, a)) return fail(error, "invalid Draco L/R edge");
            const int new_vertex = table->addVertex();
            if (new_vertex < 0 || !table->map(opposite, new_vertex))
                return fail(error, "too many Draco vertices");
            table->setLeftmost(new_vertex, opposite);
            const int right_vertex = table->vertex(table->previous(a));
            const int left_vertex = table->vertex(table->next(a));
            if (!table->map(right, right_vertex) || !table->map(left, left_vertex))
                return fail(error, "invalid Draco L/R mapping");
            table->setLeftmost(right_vertex, right);
            active.back() = corner;
            check_split = true;
        } else if (symbol == 1u) {
            if (active.empty()) return fail(error, "Draco S symbol has no active edge");
            const int b = active.back();
            active.pop_back();
            const auto split = split_active.find(static_cast<int>(sid));
            if (split != split_active.end()) active.push_back(split->second);
            if (active.empty()) return fail(error, "Draco S symbol missing second edge");
            const int a = active.back();
            if (a == b || table->oppositeCorner(a) >= 0 || table->oppositeCorner(b) >= 0 ||
                !table->setOpposite(a, corner + 2) || !table->setOpposite(b, corner + 1))
                return fail(error, "invalid Draco S topology");
            const int vp = table->vertex(table->previous(a));
            const int van = table->vertex(table->next(a));
            const int vbp = table->vertex(table->previous(b));
            if (!table->map(corner, vp) || !table->map(corner + 1, van) || !table->map(corner + 2, vbp))
                return fail(error, "invalid Draco S mapping");
            table->setLeftmost(vbp, corner + 2);
            int merge = table->next(b);
            const int merged = table->vertex(merge);
            if (vp < 0 || merged < 0) return fail(error, "invalid Draco S merge");
            merge_dest = vp;
            merge_source = merged;
            table->setLeftmost(vp, table->leftMost(merged));
            const int first = merge;
            while (merge >= 0) {
                if (!table->map(merge, vp)) return fail(error, "invalid Draco S merge mapping");
                merge = table->swingLeft(merge);
                if (merge == first) return fail(error, "closed Draco S merge ring");
            }
            table->isolate(merged);
            isolated.push_back(merged);
            active.back() = corner;
        } else if (symbol == 7u) {
            const int a = table->addVertex();
            const int b = table->addVertex();
            const int c = table->addVertex();
            if (a < 0 || b < 0 || c < 0 || !table->map(corner, a) ||
                !table->map(corner + 1, b) || !table->map(corner + 2, c))
                return fail(error, "too many Draco E vertices");
            table->setLeftmost(a, corner);
            table->setLeftmost(b, corner + 1);
            table->setLeftmost(c, corner + 2);
            active.push_back(corner);
            check_split = true;
        } else {
            return fail(error, "invalid Draco predictive topology symbol");
        }

        if (merge_dest >= 0 && !traversal.merge(merge_dest, merge_source))
            return fail(error, "invalid Draco predictive merge");
        if (!traversal.reached(*table, corner))
            return fail(error, "invalid Draco predictive valence state");

        if (check_split) {
            const int encoder_symbol = static_cast<int>(symbols) - static_cast<int>(sid) - 1;
            SplitEvent split_event;
            while (findSplit(splits, encoder_symbol, &split_cursor, &split_event)) {
                if (split_event.split < 0 || active.empty()) return fail(error, "invalid Draco topology split");
                const int top = active.back();
                const int new_active = split_event.right ? table->next(top) : table->previous(top);
                const int decoder_split = static_cast<int>(symbols) - split_event.split - 1;
                split_active[decoder_split] = new_active;
            }
        }
    }

    while (!active.empty()) {
        const int a = active.back();
        active.pop_back();
        bool interior = false;
        if (!traversal.startFace(&interior)) return fail(error, "truncated Draco start-face stream");
        if (!interior) continue;
        if (decoded_faces >= static_cast<int>(faces)) return fail(error, "too many Draco start faces");
        const int vn = table->vertex(table->next(a));
        const int b = table->next(table->leftMost(vn));
        const int vx = table->vertex(table->next(b));
        const int c = table->next(table->leftMost(vx));
        if (a == b || a == c || b == c || a < 0 || b < 0 || c < 0 ||
            table->oppositeCorner(a) >= 0 || table->oppositeCorner(b) >= 0 || table->oppositeCorner(c) >= 0)
            return fail(error, "invalid Draco interior start face");
        const int vp = table->vertex(table->next(c));
        const int new_corner = decoded_faces++ * 3;
        if (!table->setOpposite(new_corner, a) || !table->setOpposite(new_corner + 1, b) ||
            !table->setOpposite(new_corner + 2, c) || !table->map(new_corner, vx) ||
            !table->map(new_corner + 1, vp) || !table->map(new_corner + 2, vn))
            return fail(error, "invalid Draco start face");
    }

    if (decoded_faces != static_cast<int>(faces)) return fail(error, "Draco EdgeBreaker face count mismatch");
    const int vertex_count = compact(*table, isolated);
    if (vertex_count < 0 || static_cast<std::uint32_t>(vertex_count) > encoded_vertices)
        return fail(error, "Draco EdgeBreaker vertex count mismatch");
    return true;
}

bool decodePredictiveSeams(
    const CornerTable& table,
    PredictiveTraversal& traversal,
    DracoEdgeBreakerTopology *topology,
    std::string *error)
{
    if (!topology) return false;
    topology->seams.assign(
        traversal.seams.size(),
        std::vector<std::uint8_t>(table.corner_vertex.size(), 0u));
    for (std::size_t corner = 0u; corner < table.corner_vertex.size(); ++corner) {
        const int opposite = table.oppositeCorner(static_cast<int>(corner));
        if (opposite < 0) {
            for (auto& seam : topology->seams) seam[corner] = 1u;
            continue;
        }
        if (!traversal.legacy_attribute_connectivity && static_cast<std::size_t>(opposite) < corner) continue;
        for (std::size_t index = 0u; index < traversal.seams.size(); ++index) {
            bool value = false;
            if (!traversal.seams[index].bit(&value)) return fail(error, "truncated Draco seam stream");
            if (value) {
                topology->seams[index][corner] = 1u;
                topology->seams[index][static_cast<std::size_t>(opposite)] = 1u;
            }
        }
    }
    return true;
}
