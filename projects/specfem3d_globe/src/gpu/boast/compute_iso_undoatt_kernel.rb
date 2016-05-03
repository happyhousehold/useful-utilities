module BOAST

  require './compute_ani_undoatt_kernel.rb'

  def BOAST::compute_iso_undoatt_kernel(ref = true, n_gllx = 5, n_gll2 = 25,  n_gll3 = 125, n_gll3_padded = 128 )
    return compute_undoatt_kernel(:iso, ref, n_gllx, n_gll2, n_gll3, n_gll3_padded)
  end
end

