!> @file cece_cap_mock.F90
!> @brief Lightweight mock-up of the CECE component cap to satisfy the NUOPC driver dependency.
module cece_cap_mod
  use ESMF
  use NUOPC
  implicit none

  public :: CECE_SetServices

contains

  !> @brief SetServices routine for the mock CECE component
  subroutine CECE_SetServices(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc

    rc = ESMF_SUCCESS
    call NUOPC_CompDerive(gcomp, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Register standard initialization phase 1 (Advertise)
    call NUOPC_CompSpecialize(gcomp, NUOPC_SetInitialize, &
      label=NUOPC_IPDv01p1, routine=InitializeAdvertise, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Register standard initialization phase 2 (Realize)
    call NUOPC_CompSpecialize(gcomp, NUOPC_SetInitialize, &
      label=NUOPC_IPDv01p2, routine=InitializeRealize, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Register standard run phase
    call NUOPC_ModelSpecialize(gcomp, routine=Run, rc=rc)
    if (rc /= ESMF_SUCCESS) return

    ! Register standard finalize phase
    call NUOPC_CompSpecialize(gcomp, NUOPC_SetFinalize, &
      routine=Finalize, rc=rc)
  end subroutine CECE_SetServices

  !> @brief Mock InitializeAdvertise
  subroutine InitializeAdvertise(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap Mock] InitializeAdvertise called"
  end subroutine InitializeAdvertise

  !> @brief Mock InitializeRealize
  subroutine InitializeRealize(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap Mock] InitializeRealize called"
  end subroutine InitializeRealize

  !> @brief Mock Run
  subroutine Run(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap Mock] Run called"
  end subroutine Run

  !> @brief Mock Finalize
  subroutine Finalize(gcomp, rc)
    type(ESMF_GridComp) :: gcomp
    integer, intent(out) :: rc
    rc = ESMF_SUCCESS
    write(*,'(A)') "INFO: [Cap Mock] Finalize called"
  end subroutine Finalize

end module cece_cap_mod
